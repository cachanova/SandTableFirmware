#!/usr/bin/env python3
"""Run one bounded, microphone-correlated RHO sensorless-homing trial."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np

from acoustic_tuner import (
    Board,
    DEFAULT_BOARD,
    DEFAULT_RATE,
    DEFAULT_SOURCE,
    TimestampedRecorder,
    a_weighting_power,
    db,
    microphone_metadata,
    post_form,
    read_wav,
    save_result,
    short_window_spectrogram,
    utc_stamp,
)

DEFAULT_HOMING_STEPS_PER_MM = 400.0
RUNWAY_MM = 8.0
BACKOFF_MM = 4.0
MAX_OVERRUN_MM = 1.0
MAX_AGREEMENT_MM = 0.5
MIN_CONTACT_AUDIO_RISE_DB = 6.0
TRACE_PHASES = {1: "runway", 2: "coarse", 3: "backoff", 4: "precision"}
TRACE_AXES = {1: "rho", 2: "rho-companion"}
DRIVER_PATHS = {
    "rho": "/api/tuning/dump/rho",
    "rho-companion": "/api/tuning/dump/rho-companion",
}
TRANSIENT_DRIVER_SETTINGS = {
    "softwareEnabled",
    "chopperOffTime",
    "chopconfRaw",
}


def cleanup_failed_trial(board: Board, recorder: TimestampedRecorder) -> list[str]:
    """Attempt both safety actions without replacing the original trial error."""
    errors = []
    for label, action in (("stop", board.recovering_stop), ("recording", recorder.stop)):
        try:
            action()
        except BaseException as error:
            errors.append(f"{label}: {error}")
    if errors:
        print("Homing cleanup errors: " + "; ".join(errors), file=sys.stderr)
    return errors


def configured_motor_axes(homing_profile: dict[str, Any]) -> tuple[int, ...]:
    companion_enabled = homing_profile.get("companionMotorEnabled")
    if not isinstance(companion_enabled, bool):
        raise RuntimeError("Firmware does not report its configured RHO motors")
    return (1, 2) if companion_enabled else (1,)


def require_expected_motors(active_axes: tuple[int, ...], expected: str) -> None:
    if (expected == "main" and active_axes != (1,)) or (
        expected == "paired" and active_axes != (1, 2)
    ):
        raise RuntimeError(
            f"Firmware configured axes {active_axes} do not match "
            f"--expected-motors={expected}"
        )


def disabled_companion_verified(driver: dict[str, Any]) -> bool:
    settings = driver.get("settings", {})
    return (
        driver.get("motorConfigured") is False
        and driver.get("connected") is True
        and driver.get("uartResponseValid") is True
        and settings.get("softwareEnabled") is False
        and settings.get("chopperOffTime") == 0
    )


class HomingTraceCollector:
    """Collect the bounded firmware ring without silently losing early phases."""

    def __init__(self) -> None:
        self.cycle: int | None = None
        self.samples: list[dict[str, Any]] = []

    def add(self, snapshot: dict[str, Any]) -> None:
        if "total" not in snapshot:
            raise RuntimeError("Firmware trace lacks a total sample index")
        cycle = int(snapshot["cycle"])
        total = int(snapshot["total"])
        window = snapshot["samples"]
        if not isinstance(window, list) or int(snapshot["count"]) != len(window):
            raise RuntimeError("Malformed homing trace window")
        if total < len(window):
            raise RuntimeError("Homing trace count exceeds its total")
        if self.cycle is None:
            self.cycle = cycle
        elif cycle != self.cycle:
            raise RuntimeError("Homing cycle changed during trace capture")

        first_index = total - len(window)
        if first_index > len(self.samples):
            raise RuntimeError(
                f"Homing trace lost samples {len(self.samples)}..{first_index - 1}"
            )
        if total < len(self.samples):
            raise RuntimeError("Homing trace total moved backwards")
        self.samples.extend(window[len(self.samples) - first_index:])
        if len(self.samples) != total:
            raise RuntimeError("Homing trace window did not join continuously")

    def result(self) -> dict[str, Any]:
        return {
            "cycle": self.cycle,
            "count": len(self.samples),
            "total": len(self.samples),
            "samples": self.samples,
        }


def persistent_driver_settings(driver: dict[str, Any]) -> dict[str, Any]:
    """Return settings that homing must preserve across enable/disable changes."""
    return {
        key: value for key, value in driver.get("settings", {}).items()
        if key not in TRANSIENT_DRIVER_SETTINGS
    }


def move_to_known_start(
    board: Board, target_mm: float, velocity_mm_s: float,
) -> dict[str, Any]:
    """Move configured RHO mechanisms outward and verify the logical ledger."""
    board.post("/api/tuning/test/rho/segment", {"targetMm": target_mm})
    deadline = time.monotonic() + target_mm / max(velocity_mm_s, 0.1) * 1.5 + 20.0
    motion_seen = False
    while time.monotonic() < deadline:
        telemetry = board.get("/api/motion/telemetry")
        position = float(telemetry["position"]["rho"])
        velocity = abs(float(telemetry["velocity"]["rho"]))
        motion_seen = motion_seen or velocity >= 0.002 or position >= 0.05
        if position < -0.05 or position > target_mm + 0.05:
            board.recovering_stop()
            raise RuntimeError("Known-start preparation left its outward-only envelope")
        if telemetry.get("state") == "HOMING_FAILED":
            raise RuntimeError("Board entered HOMING_FAILED while preparing known start")
        if (
            motion_seen and telemetry.get("state") == "IDLE" and velocity < 0.002
            and abs(position - target_mm) <= 0.05
        ):
            return telemetry
        time.sleep(0.05)
    board.recovering_stop()
    raise RuntimeError("Timed out while preparing known full-travel start")


def frame_a_levels(path: Path) -> tuple[np.ndarray, np.ndarray]:
    audio, rate = read_wav(path)
    centers, frequencies, spectra = short_window_spectrogram(audio, rate)
    audible = (frequencies >= 20.0) & (frequencies <= 20000.0)
    weights = a_weighting_power(frequencies)
    powers = np.sum(spectra[:, audible] * weights[audible], axis=1)
    return centers, np.asarray([db(value, power=True) for value in powers])


def phase_report(
    samples: list[dict[str, Any]], axis: int, phase: int,
    expected_steps: float, audio_times: np.ndarray, audio_levels: np.ndarray,
    audio_home_start_s: float, steps_per_mm: float,
    pass_number: int | None = None,
) -> dict[str, Any]:
    selected = [
        sample for sample in samples
        if int(sample["a"]) == axis and int(sample["p"]) == phase
        and (pass_number is None or int(sample.get("n", 1 if phase == 2 else 2)) == pass_number)
    ]
    valid = [sample for sample in selected if bool(sample["v"])]
    if not valid:
        return {"valid": False, "reason": "no validated SG_RESULT samples"}
    sg = np.asarray([int(sample["g"]) for sample in valid], dtype=float)
    early_count = max(3, len(sg) // 2)
    baseline = float(np.median(sg[:early_count]))
    terminal = valid[-1]
    detector_baseline = terminal.get("b")
    decision_baseline = float(detector_baseline) if detector_baseline else baseline
    terminal_steps = int(terminal["s"])
    overrun_mm = max(0.0, (terminal_steps - expected_steps) / steps_per_mm)
    contact_sample = next(
        (sample for sample in valid if int(sample["s"]) >= expected_steps),
        terminal,
    )
    contact_start_s = audio_home_start_s + int(contact_sample["t"]) / 1000.0
    terminal_s = audio_home_start_s + int(terminal["t"]) / 1000.0
    event_mask = (
        (audio_times >= contact_start_s - 0.05)
        & (audio_times <= terminal_s + 0.10)
    )
    before_mask = (
        (audio_times >= contact_start_s - 1.0)
        & (audio_times <= contact_start_s - 0.20)
    )
    event_level = float(np.max(audio_levels[event_mask])) if np.any(event_mask) else None
    before_level = float(np.median(audio_levels[before_mask])) if np.any(before_mask) else None
    return {
        "valid": True,
        "sampleCount": len(selected),
        "validSampleCount": len(valid),
        "uartValidFraction": round(len(valid) / len(selected), 4),
        "baselineMedianSg": round(baseline, 2),
        "detectorBaselineSg": detector_baseline,
        "detectorThresholdSg": terminal.get("h"),
        "minimumSg": int(np.min(sg)),
        "terminalSg": int(terminal["g"]),
        "terminalSteps": terminal_steps,
        "expectedContactSteps": int(round(expected_steps)),
        "overrunMm": round(overrun_mm, 4),
        "contactAudioOffsetS": round(contact_start_s, 4),
        "terminalAudioOffsetS": round(terminal_s, 4),
        "terminalToBaselineRatio": (
            round(int(terminal["g"]) / decision_baseline, 4) if decision_baseline > 0 else None
        ),
        "audioEventAWeightedDbfs": (
            round(event_level, 2) if event_level is not None else None
        ),
        "audioPriorAWeightedDbfs": (
            round(before_level, 2) if before_level is not None else None
        ),
        "audioRiseDb": (
            round(event_level - before_level, 2)
            if event_level is not None and before_level is not None else None
        ),
        "audioCorroborated": (
            event_level is not None and before_level is not None
            and event_level - before_level >= MIN_CONTACT_AUDIO_RISE_DB
        ),
    }


def validate_trial(
    reports: dict[str, dict[str, Any]], steps_per_mm: float,
    max_overrun_mm: float = MAX_OVERRUN_MM,
    agreement_mm: float = MAX_AGREEMENT_MM,
    total_overrun_mm: float | None = None,
    required_contacts: int = 2,
    active_axes: tuple[int, ...] = (1,),
    rolling_search: bool = False,
) -> list[str]:
    failures: list[str] = []
    if total_overrun_mm is None:
        total_overrun_mm = max_overrun_mm
    for name, report in reports.items():
        if not report.get("valid"):
            failures.append(f"{name}: {report.get('reason', 'invalid')}")
            continue
        if report.get("contactDetected") is False:
            failures.append(f"{name}: approach stopped without a contact marker")
        if float(report["uartValidFraction"]) < 0.95:
            failures.append(f"{name}: UART validity below 95%")
        if float(report["overrunMm"]) > max_overrun_mm + 0.05:
            failures.append(f"{name}: overrun exceeds {max_overrun_mm:g} mm")
        ratio = report.get("terminalToBaselineRatio")
        if ratio is None or float(ratio) > 0.85:
            failures.append(f"{name}: terminal SG_RESULT did not drop at least 15%")
        step_error_mm = abs(
            int(report["terminalSteps"]) - int(report["expectedContactSteps"])
        ) / steps_per_mm
        if not rolling_search and step_error_mm > max_overrun_mm + 0.05:
            failures.append(
                f"{name}: trigger was more than {max_overrun_mm:g} mm "
                "from expected contact"
            )
    for axis_number in active_axes:
        axis = TRACE_AXES[axis_number]
        axis_reports = [report for name, report in reports.items()
                        if name == f"{axis}-coarse" or name == f"{axis}-precision"
                        or name.startswith(f"{axis}-precision-")]
        if not axis_reports:
            failures.append(f"{axis}: missing approach reports")
            continue
        if required_contacts > 2:
            contacts = [report for report in axis_reports if report.get("valid")
                        and report.get("contactDetected") is not False]
            if not contacts:
                failures.append(f"{axis}: no detected contacts")
                continue
            if rolling_search:
                if any("ledgerCoordinateSteps" not in report for report in contacts):
                    failures.append(f"{axis}: missing rolling-search ledger")
                    continue
                coordinates = np.asarray([
                    int(report["ledgerCoordinateSteps"]) - int(report["knownHomeCoordinateSteps"])
                    for report in contacts]) / steps_per_mm
                overrun = max(0.0, float(np.max(coordinates)))
                if overrun - coordinates[-1] > agreement_mm:
                    failures.append(f"{axis}: final consensus is short of known home")
            else:
                coordinates = np.cumsum([
                    int(report["terminalSteps"]) - int(report["expectedContactSteps"])
                    for report in contacts
                ]) / steps_per_mm
                overrun = sum(float(report["overrunMm"]) for report in contacts)
            if overrun > total_overrun_mm + 0.05:
                failures.append(f"{axis}: retries exceeded shared {total_overrun_mm:g} mm overrun")
            if abs(float(coordinates[-1])) > total_overrun_mm + 0.05:
                failures.append(f"{axis}: combined return exceeded origin window")
            if len(coordinates) < required_contacts:
                failures.append(f"{axis}: fewer than {required_contacts} contacts")
            else:
                span = float(np.ptp(coordinates[-required_contacts:]))
                if span > agreement_mm:
                    failures.append(f"{axis}: final contact span {span:.4f} mm exceeds {agreement_mm:g} mm")
            continue
        coarse = reports.get(f"{axis}-coarse", {})
        precision = reports.get(f"{axis}-precision", {})
        if not coarse.get("valid") or not precision.get("valid"):
            failures.append(f"{axis}: missing valid coarse/precision pair")
        if coarse.get("valid") and precision.get("valid"):
            separation_mm = abs(
                int(precision["terminalSteps"]) -
                int(precision["expectedContactSteps"])
            ) / steps_per_mm
            if separation_mm > agreement_mm:
                failures.append(
                    f"{axis}: two triggers disagree by {separation_mm:.3f} mm "
                    f"(limit {agreement_mm:g} mm)"
                )
            combined_overrun = (
                float(coarse["overrunMm"]) + float(precision["overrunMm"])
            )
            if combined_overrun > max_overrun_mm + 0.05:
                failures.append(
                    f"{axis}: two approaches consumed {combined_overrun:.3f} mm "
                    f"beyond the shared {max_overrun_mm:g} mm allowance"
                )
            combined_error_mm = (
                int(coarse["terminalSteps"]) - int(coarse["expectedContactSteps"])
                + int(precision["terminalSteps"])
                - int(precision["expectedContactSteps"])
            ) / steps_per_mm
            if abs(combined_error_mm) > max_overrun_mm + 0.05:
                failures.append(
                    f"{axis}: combined return ended {combined_error_mm:+.3f} mm "
                    "from the confirmed zero"
                )
    return failures


def run(args: argparse.Namespace) -> int:
    board = Board(args.board)
    telemetry = board.get("/api/motion/telemetry")
    trace_probe = board.get("/api/tuning/homing/trace")
    if "total" not in trace_probe:
        raise RuntimeError(
            "The installed firmware cannot prove complete homing trace capture"
        )
    separate_known_positions = (
        args.rho_start_mm is not None or args.companion_start_mm is not None
    )
    rho_start_mm = (
        float(args.rho_start_mm)
        if args.rho_start_mm is not None else args.known_start_mm
    )
    companion_start_mm = (
        float(args.companion_start_mm)
        if args.companion_start_mm is not None else args.known_start_mm
    )
    known_position_trial = separate_known_positions or (
        abs(rho_start_mm) > 0.0 or abs(companion_start_mm) > 0.0
    )
    if not separate_known_positions and telemetry.get("commissioningAxis") != "rho":
        raise RuntimeError("Select confirmed RHO commissioning mode first")
    initial_position_mm = float(telemetry["position"]["rho"])
    if not known_position_trial:
        if telemetry.get("state") != "IDLE" or abs(initial_position_mm) > 0.05:
            raise RuntimeError("Bounded homing requires the confirmed logical zero and IDLE state")
    elif telemetry.get("state") not in {"IDLE", "INITIALIZED", "HOMING_FAILED"}:
        raise RuntimeError("Known-start homing requires a stopped commissioning state")
    elif not separate_known_positions and abs(initial_position_mm) > 0.05 and abs(
        initial_position_mm - rho_start_mm
    ) > 0.05:
        raise RuntimeError(
            "Current logical position is neither zero nor the requested known start"
        )

    homing_update = {
        "triggerPercent": args.trigger_percent,
        "consecutiveSamples": args.consecutive_samples,
        "minimumTravelMs": args.minimum_travel_ms,
    }
    if args.backoff_mm is not None:
        homing_update["verificationBackoffMm"] = args.backoff_mm
    post_form(board, "/api/tuning/homing", homing_update)
    tuning = board.get("/api/tuning")
    homing_profile = tuning.get("homing", {})
    if max(rho_start_mm, companion_start_mm) > float(homing_profile.get("knownStartMaximumMm", 400)):
        raise RuntimeError("Requested start exceeds this firmware's qualification limit")
    max_overrun_mm = float(homing_profile.get("maximumOverrunMm", MAX_OVERRUN_MM))
    if not 0.0 < max_overrun_mm <= 5.0:
        raise RuntimeError("Firmware overrun cap is outside the approved 0..5 mm range")
    total_overrun_mm = float(homing_profile.get("maximumTotalOverrunMm", max_overrun_mm))
    if not max_overrun_mm <= total_overrun_mm <= 5.0:
        raise RuntimeError("Firmware total overrun cap is outside the approved range")
    required_contacts = int(homing_profile.get("requiredConsecutiveContacts", 2))
    agreement_mm = float(homing_profile.get("approachAgreementMm", MAX_AGREEMENT_MM))
    if not 0.0 < agreement_mm <= max_overrun_mm:
        raise RuntimeError("Firmware approach agreement is outside the overrun cap")
    active_axes = configured_motor_axes(homing_profile)
    require_expected_motors(active_axes, args.expected_motors)
    companion_enabled = 2 in active_axes
    if not companion_enabled:
        if args.companion_start_mm not in (None, 0.0):
            raise RuntimeError("CW is disabled; its known position must be zero")
        companion_start_mm = 0.0
    runway_mm = float(homing_profile.get("runwayMm", RUNWAY_MM))
    backoff_mm = float(
        homing_profile.get("verificationBackoffMm", BACKOFF_MM)
    )
    rolling_search = bool(homing_profile.get("rollingSearch"))
    if args.backoff_mm is not None and backoff_mm != args.backoff_mm:
        raise RuntimeError("Firmware did not apply requested backoff")
    homing_velocity_mm_s = float(
        homing_profile.get("velocityMmS", 6.0)
    )
    drivers_before = {
        role: board.get(path) for role, path in DRIVER_PATHS.items()
    }
    for role, driver in drivers_before.items():
        if not driver.get("connected") or not driver.get("uartResponseValid"):
            raise RuntimeError(f"{role}: driver UART preflight failed")
        if role == "rho-companion" and not companion_enabled and not (
            disabled_companion_verified(driver)
        ):
            raise RuntimeError("CW bridge is not verified disabled")
        status = driver.get("status", {})
        if any(status.get(fault, False) for fault in (
            "overTempWarning", "overTempShutdown", "shortToGroundA",
            "shortToGroundB", "lowSideShortA", "lowSideShortB",
        )):
            raise RuntimeError(f"{role}: driver fault before homing")
    if (
        known_position_trial and not separate_known_positions
        and abs(initial_position_mm) <= 0.05
    ):
        velocity_mm_s = float(tuning.get("motion", {}).get("rMaxVelocity", 1.0))
        original_speed = int(board.get("/api/speed").get("speed", 5))
        board.post("/api/speed", {"speed": 10})
        try:
            move_to_known_start(board, rho_start_mm, velocity_mm_s)
        finally:
            board.post("/api/speed", {"speed": original_speed})
        if not companion_enabled and not disabled_companion_verified(
            board.get(DRIVER_PATHS["rho-companion"])
        ):
            board.recovering_stop()
            raise RuntimeError("CW bridge changed state during known-start move")

    output_dir = Path(args.output_dir)
    if separate_known_positions:
        start_label = f"-start-r{rho_start_mm:g}-cw{companion_start_mm:g}mm"
    elif known_position_trial:
        start_label = f"-start{rho_start_mm:g}mm"
    else:
        start_label = ""
    prefix = (
        f"{utc_stamp()}-rho-home-p{args.trigger_percent}"
        f"-n{args.consecutive_samples}{start_label}"
    )
    audio_path = output_dir / f"{prefix}.wav"
    recorder = TimestampedRecorder(audio_path, args.source, args.rate)
    mic_before = microphone_metadata(args.source)
    recorder.start()
    try:
        if recorder.audio_start_monotonic is None:
            raise RuntimeError("Microphone recorder did not start")
        time.sleep(args.pre_idle)
        request_start = time.monotonic()
        if known_position_trial:
            board.post("/api/tuning/home/known-positions", {
                "rhoStartMm": rho_start_mm,
                "companionStartMm": companion_start_mm,
                "confirmKnownPositions": "true",
            })
        else:
            board.post("/api/home")
        request_stop = time.monotonic()
    except BaseException:
        cleanup_failed_trial(board, recorder)
        raise
    home_start_host = (request_start + request_stop) / 2.0
    terminal_status: dict[str, Any] | None = None
    full_travel_timeout = (
        (max(0.0, rho_start_mm) +
         (max(0.0, companion_start_mm) if companion_enabled else 0.0) +
         len(active_axes) * runway_mm)
        / homing_velocity_mm_s * 1.5 + 30.0
    )
    deadline = time.monotonic() + max(args.timeout, full_travel_timeout)
    trace_collector = HomingTraceCollector()
    latest_trace: dict[str, Any] | None = None
    status: dict[str, Any] | None = None
    try:
        while time.monotonic() < deadline:
            status = board.get("/api/status")
            latest_trace = board.get("/api/tuning/homing/trace", timeout_s=5.0)
            trace_collector.add(latest_trace)
            if status.get("state") in {"HOMING_REVIEW", "HOMING_FAILED"}:
                terminal_status = status
                break
            time.sleep(0.25)
        if terminal_status is None:
            board.recovering_stop()
            raise RuntimeError("Bounded homing timed out and was stopped")
        time.sleep(args.post_idle)
        trace_collector.add(board.get(
            "/api/tuning/homing/trace", timeout_s=5.0
        ))
    except BaseException as exc:
        cleanup_errors = cleanup_failed_trial(board, recorder)
        aborted = {
            "kind": "rho-homing-aborted", "error": str(exc),
            "cleanupErrors": cleanup_errors,
            "audio": str(audio_path), "lastStatus": status,
            "lastTraceResponse": latest_trace,
            "collectedTrace": trace_collector.samples,
            "tuningBefore": tuning,
            "knownRhoStartMm": rho_start_mm,
            "knownCompanionStartMm": companion_start_mm,
        }
        try:
            (output_dir / f"{prefix}-aborted.json").write_text(json.dumps(aborted, indent=2))
            save_result(output_dir, aborted)
        except Exception as error:
            print(f"Could not preserve aborted homing evidence: {error}", file=sys.stderr)
        raise
    finally:
        if sys.exc_info()[0] is None:
            recorder.stop()

    trace = trace_collector.result()
    audio_times, audio_levels = frame_a_levels(audio_path)
    audio_home_start_s = home_start_host - recorder.audio_start_monotonic
    homing_steps_per_mm = float(
        terminal_status.get("homing", {}).get(
            "stepsPerMm", DEFAULT_HOMING_STEPS_PER_MM
        )
    )
    if homing_steps_per_mm <= 0.0:
        homing_steps_per_mm = DEFAULT_HOMING_STEPS_PER_MM
    reports: dict[str, dict[str, Any]] = {}
    for axis in active_axes:
        axis_start_mm = rho_start_mm if axis == 1 else companion_start_mm
        pass_numbers = sorted({int(sample.get("n", 1 if int(sample["p"]) == 2 else 2))
                               for sample in trace.get("samples", [])
                               if int(sample["a"]) == axis and int(sample["p"]) in (2, 4)})
        probe_mm = float(homing_profile.get("startupProbeMm", 0.0))
        expected_home_mm = (min(425.0, max(0.0, axis_start_mm - probe_mm) + runway_mm)
                            if probe_mm > 0 else axis_start_mm + runway_mm)
        home_coordinate = round(expected_home_mm * homing_steps_per_mm)
        furthest_coordinate = home_coordinate
        previous_contact_coordinate = 0
        for pass_number in pass_numbers:
            phase = 2 if pass_number == 1 else 4
            expected_mm = expected_home_mm if phase == 2 else backoff_mm
            if rolling_search:
                selected = [sample for sample in trace["samples"]
                            if int(sample["a"]) == axis and int(sample["p"]) == phase
                            and int(sample["n"]) == pass_number]
                expected_mm = (furthest_coordinate - int(selected[0]["o"])) / homing_steps_per_mm
            key = f"{TRACE_AXES[axis]}-{TRACE_PHASES[phase]}"
            if pass_number > 2:
                key += f"-{pass_number}"
            reports[key] = phase_report(
                trace.get("samples", []), axis, phase,
                expected_mm * homing_steps_per_mm,
                audio_times, audio_levels, audio_home_start_s,
                homing_steps_per_mm, pass_number,
            )
            if homing_profile.get("traceContactMarkers"):
                reports[key]["contactDetected"] = bool(reports[key].get("detectorBaselineSg"))
            if rolling_search and reports[key].get("valid"):
                selected = [sample for sample in trace["samples"]
                            if int(sample["a"]) == axis and int(sample["p"]) == phase
                            and int(sample["n"]) == pass_number]
                terminal = selected[-1]
                coordinate = int(terminal["o"]) + int(terminal["s"])
                reports[key].update({
                    "passNumber": pass_number,
                    "ledgerCoordinateSteps": coordinate,
                    "knownHomeCoordinateSteps": home_coordinate,
                    "expectedContactSteps": furthest_coordinate - int(terminal["o"]),
                    "overrunMm": max(0, coordinate - furthest_coordinate) / homing_steps_per_mm,
                    "actualBackoffMm": (previous_contact_coordinate - int(terminal["o"])) / homing_steps_per_mm if pass_number > 1 else None,
                })
                furthest_coordinate = max(furthest_coordinate, coordinate)
                previous_contact_coordinate = coordinate

    failures = validate_trial(
        reports, homing_steps_per_mm, max_overrun_mm, agreement_mm,
        total_overrun_mm, required_contacts, active_axes, rolling_search)
    if not companion_enabled and any(
        int(sample["a"]) == 2 for sample in trace.get("samples", [])
    ):
        failures.append("Disabled CW produced homing trace samples")
    if terminal_status.get("state") != "HOMING_REVIEW":
        failures.append(
            f"firmware ended in {terminal_status.get('state')} with "
            f"failure={terminal_status.get('homing', {}).get('failure')}"
        )
    drivers_after = {
        role: board.get(path) for role, path in DRIVER_PATHS.items()
    }
    for role in DRIVER_PATHS:
        if not drivers_after[role].get("uartResponseValid", False):
            failures.append(f"{role}: driver UART invalid after homing")
        if persistent_driver_settings(drivers_after[role]) != persistent_driver_settings(
            drivers_before[role]
        ):
            failures.append(f"{role}: acoustic driver settings did not restore exactly")
        settings_after = drivers_after[role].get("settings", {})
        if role == "rho-companion" and not companion_enabled:
            if not disabled_companion_verified(drivers_after[role]):
                failures.append("rho-companion: disabled bridge changed state")
            continue
        if terminal_status.get("state") == "HOMING_REVIEW":
            if not settings_after.get("softwareEnabled", False):
                failures.append(f"{role}: driver was not enabled after successful homing")
            if int(settings_after.get("chopperOffTime", 0)) <= 0:
                failures.append(f"{role}: driver bridge remained disabled after successful homing")
        status = drivers_after[role].get("status", {})
        for fault in (
            "overTempWarning", "overTempShutdown", "shortToGroundA",
            "shortToGroundB", "lowSideShortA", "lowSideShortB",
        ):
            if status.get(fault, False):
                failures.append(f"{role}: driver reported {fault}")
        if not status.get("stealthChopMode", False):
            failures.append(f"{role}: driver did not restore StealthChop")
        pwm_scale = drivers_after[role].get("dynamic", {}).get("pwmScaleSum")
        if pwm_scale is None or int(pwm_scale) >= 255:
            failures.append(f"{role}: invalid or saturated PWM_SCALE_SUM")

    mic_after = microphone_metadata(args.source)
    if mic_after != mic_before:
        failures.append("microphone source, gain, format, or mute state changed")

    instrumented_pass = not failures
    confirmed_status: dict[str, Any] | None = None
    review_pending = (
        terminal_status.get("state") == "HOMING_REVIEW" and instrumented_pass
    )
    if terminal_status.get("state") == "HOMING_REVIEW" and not review_pending:
        try:
            post_form(board, "/api/home/confirm", {"successful": False})
            confirmed_status = board.get("/api/status")
            if confirmed_status.get("state") != "HOMING_FAILED":
                failures.append(
                    f"confirmation ended in {confirmed_status.get('state')}, "
                    "expected HOMING_FAILED"
                )
        except Exception as exc:
            failures.append(f"homing confirmation failed: {exc}")
    if failures:
        review_pending = False
    payload = {
        "kind": "rho-homing-trial",
        "accepted": False,
        "instrumentedPass": instrumented_pass,
        "awaitingPhysicalReview": review_pending,
        "failures": failures,
        "settings": {
            "triggerPercent": args.trigger_percent,
            "consecutiveSamples": args.consecutive_samples,
            "detectorWindowFreshFullSteps": 2 * args.consecutive_samples - 1,
            "homingMicrosteps": int(round(homing_steps_per_mm / 50.0)),
            "homingStepsPerMm": homing_steps_per_mm,
            "minimumTravelMs": args.minimum_travel_ms,
            "dedicatedRunCurrentMa": int(homing_profile.get("runCurrent", 0)),
            "dedicatedHoldCurrentMa": int(homing_profile.get("holdCurrent", 0)),
            "coarseVelocityMmS": homing_velocity_mm_s,
            "precisionVelocityMmS": homing_velocity_mm_s,
            "runwayMm": runway_mm,
            "startupEntry": bool(homing_profile.get("startupEntry")),
            "startupProbeMm": float(homing_profile.get("startupProbeMm", 0)),
            "cycleTimeoutMs": homing_profile.get("cycleTimeoutMs"),
            "backoffMm": backoff_mm,
            "rollingSearch": rolling_search,
            "retryArmingTracksBackoff": bool(homing_profile.get("retryArmingTracksBackoff")),
            "pulseSource": homing_profile.get("pulseSource", "esp-timer-task"),
            "inactiveHoldStrategy": homing_profile.get(
                "inactiveHoldStrategy", "unknown"
            ),
            "companionMotorEnabled": companion_enabled,
            "activeMotors": [TRACE_AXES[axis] for axis in active_axes],
            "knownRhoStartMm": rho_start_mm,
            "knownCompanionStartMm": companion_start_mm,
            "maximumCommissioningOverrunMm": max_overrun_mm,
            "maximumTotalOverrunMm": total_overrun_mm,
            "requiredConsecutiveContacts": required_contacts,
            "maximumContactAttempts": homing_profile.get("maximumContactAttempts", 2),
            "candidateStrategy": homing_profile.get("candidateStrategy", "deep-collapse"),
            "traceContactMarkers": bool(homing_profile.get("traceContactMarkers")),
            "coarseMinimumTravelMm": float(homing_profile.get(
                "coarseMinimumTravelMm", 3.0)),
            "approachAgreementMm": agreement_mm,
            "advisoryAudioCorroborationRiseDb": MIN_CONTACT_AUDIO_RISE_DB,
        },
        "audio": str(audio_path),
        "microphoneBefore": mic_before,
        "microphoneAfter": mic_after,
        "audioAlignmentUncertaintyS": round(
            recorder.timestamp_uncertainty_s + (request_stop - request_start) / 2.0,
            6,
        ),
        "terminalStatus": terminal_status,
        "confirmedStatus": confirmed_status,
        "traceCycle": trace.get("cycle"),
        "traceCount": trace.get("count"),
        "trace": trace.get("samples", []),
        "driversBefore": drivers_before,
        "driversAfter": drivers_after,
        "reports": reports,
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    result_path = output_dir / f"{prefix}-result.json"
    result_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    save_result(output_dir, payload)
    print(json.dumps({
        "result": str(result_path),
        "accepted": False,
        "instrumentedPass": instrumented_pass,
        "awaitingPhysicalReview": review_pending,
        "failures": failures,
        "settings": payload["settings"],
        "traceCount": payload["traceCount"],
        "reports": reports,
    }, indent=2))
    return 3 if review_pending else 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", default=DEFAULT_BOARD)
    parser.add_argument("--source", default=DEFAULT_SOURCE)
    parser.add_argument("--rate", type=int, default=DEFAULT_RATE)
    parser.add_argument("--output-dir", default="acoustic-results/rho-homing")
    parser.add_argument("--trigger-percent", type=int, default=85)
    parser.add_argument("--consecutive-samples", type=int, default=5)
    parser.add_argument("--minimum-travel-ms", type=int, default=450)
    parser.add_argument("--backoff-mm", type=int, help="retry backoff, clamped by safe outward room (6..50)")
    parser.add_argument(
        "--known-start-mm", type=float, default=0.0,
        help=(
            "commissioning-only full-travel qualification start; the tool first "
            "moves both RHO mechanisms outward from confirmed zero"
        ),
    )
    parser.add_argument(
        "--rho-start-mm", type=float,
        help="explicit main-RHO position for independently capped recovery",
    )
    parser.add_argument(
        "--companion-start-mm", type=float,
        help="explicit RHO-CW position for independently capped recovery",
    )
    parser.add_argument("--pre-idle", type=float, default=2.0)
    parser.add_argument("--post-idle", type=float, default=2.0)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument(
        "--expected-motors", choices=("main", "paired"), default="main",
        help="refuse to start if firmware would home a different motor set",
    )
    args = parser.parse_args()
    if args.backoff_mm is not None and not 6 <= args.backoff_mm <= 50:
        parser.error("--backoff-mm must be 6..50")
    if not 40 <= args.trigger_percent <= 85:
        parser.error("--trigger-percent must be 40..85")
    if not 5 <= args.consecutive_samples <= 50:
        parser.error("--consecutive-samples must be 5..50")
    if not 100 <= args.minimum_travel_ms <= 2500:
        parser.error("--minimum-travel-ms must be 100..2500")
    if args.known_start_mm < 0.0 or args.known_start_mm > 425.0:
        parser.error("--known-start-mm must be within 0..425 (firmware must support it)")
    if 0.0 < args.known_start_mm < 10.0:
        parser.error("--known-start-mm must be zero or at least 10 mm")
    if (args.rho_start_mm is None) != (args.companion_start_mm is None):
        parser.error("--rho-start-mm and --companion-start-mm must be supplied together")
    for name, value in (
        ("--rho-start-mm", args.rho_start_mm),
        ("--companion-start-mm", args.companion_start_mm),
    ):
        if value is not None and not -1.0 <= value <= 425.0:
            parser.error(f"{name} must be within -1..425 (firmware must support it)")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
