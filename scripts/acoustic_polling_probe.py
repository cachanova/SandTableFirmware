#!/usr/bin/env python3
"""Stationary, read-only telemetry/UART audio A/B/A diagnostic; never a tune pass."""
import argparse
import datetime as dt
import json
import math
import time
from pathlib import Path

import numpy as np

from acoustic_tuner import (
    AXES, DEFAULT_BOARD, DEFAULT_SOURCE, DEFAULT_RATE, Board, CruiseDriverGuard,
    TimestampedRecorder, _telemetry_sample, microphone_metadata, read_wav,
    short_window_spectrogram, a_weighting_power, db, raw_percentile_upper_95,
)


def require_unchanged_idle(sample: dict, initial: dict) -> None:
    """Fail on any motion, coordinate/epoch change, reset, or incomplete data."""
    if (sample["state"] != "IDLE" or sample["stepMotion"]["active"]
            or sample["stepMotion"]["epoch"] != initial["stepMotion"]["epoch"]
            or sample["planner"]["timerActive"] or sample["planner"]["running"]
            or sample["planner"]["underruns"] != initial["planner"]["underruns"]):
        raise RuntimeError("Stationary polling probe observed motion or changed STEP state")
    for axis in ("rho", "theta"):
        position = float(sample["position"][axis])
        velocity = float(sample["velocity"][axis])
        if (not math.isfinite(position) or not math.isfinite(velocity)
                or position != initial["position"][axis] or velocity != 0):
            raise RuntimeError("Stationary polling probe observed a coordinate/velocity change")


def summarize_power(values: np.ndarray) -> dict:
    upper_power = raw_percentile_upper_95(values, 95, 321)
    return dict(rawMedianAWeightedDbfs=db(np.median(values), power=True),
                rawP95AWeightedDbfs=db(np.percentile(values, 95), power=True),
                rawP95Upper95AWeightedDbfs=(db(upper_power, power=True)
                                           if upper_power is not None else None),
                fractionAboveMinus55=float(np.mean(values > 10 ** (-55 / 10))))


def analyze_report(path: Path) -> dict:
    report = json.loads(path.read_text())
    if not report.get("completed"):
        raise RuntimeError("Cannot analyze an incomplete stationary probe")
    audio, rate = read_wav(path.with_suffix(".wav"))
    times, frequencies, spectra = short_window_spectrogram(audio, rate)
    audible = (frequencies >= 20) & (frequencies <= 20000)
    weighted = spectra[:, audible] @ a_weighting_power(frequencies[audible])
    host_times = times + report["audioStartHostS"]
    for block in report["blocks"]:
        values = weighted[(host_times > block["startHostS"] + 1)
                          & (host_times < block["endHostS"] - 1)]
        block.update(summarize_power(values))
    report["analyzed"] = True
    path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"result": str(path), "blocks": report["blocks"]}, indent=2))
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", default=DEFAULT_BOARD)
    parser.add_argument("--source", default=DEFAULT_SOURCE)
    parser.add_argument("--block-seconds", type=float, default=20)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--analyze", type=Path, help="Re-analyze a completed JSON/WAV, no board access")
    args = parser.parse_args()
    if args.analyze:
        analyze_report(args.analyze)
        return
    if args.output_dir is None:
        parser.error("--output-dir is required for a new stationary probe")
    if not math.isfinite(args.block_seconds) or not 5 <= args.block_seconds <= 60:
        parser.error("block duration must be 5..60 seconds")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    prefix = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    path = args.output_dir / f"{prefix}-stationary-polling"
    board = Board(args.board)
    zero = time.monotonic()
    initial = board.get("/api/motion/telemetry")
    require_unchanged_idle(initial, initial)
    guard = CruiseDriverGuard()
    initial_drivers = []
    for role, endpoint in AXES["rho"].driver_dump_paths:
        driver = board.get(endpoint)
        guard.observe(role, driver, "stationary-probe")
        initial_drivers.append({"role": role, "driver": driver})
    gain_before = microphone_metadata(args.source)
    if (gain_before.get("mute") != "no" or not gain_before.get("captureVolume")
            or "metadataWarning" in gain_before):
        raise RuntimeError("Cannot establish an unmuted fixed-gain microphone")
    recorder = TimestampedRecorder(path.with_suffix(".wav"), args.source, DEFAULT_RATE)
    samples, driver_samples, blocks = [], [], []
    report = {"purpose": "Stationary observation-only polling attribution; not sound qualification",
              "initialTelemetry": initial, "initialDrivers": initial_drivers,
              "gainBefore": gain_before, "blocks": blocks, "telemetry": samples,
              "driverSamples": driver_samples, "completed": False}
    recorder.start()
    try:
        report["audioStartHostS"] = recorder.audio_start_monotonic - zero
        report["audioEpochUncertaintyS"] = recorder.timestamp_uncertainty_s
        for index, with_drivers in enumerate((False, True, False, True, False)):
            block = {"index": index, "driverPolling": with_drivers,
                     "startHostS": time.monotonic() - zero}
            blocks.append(block)
            deadline = time.monotonic() + args.block_seconds
            next_telemetry = next_driver = 0.0
            while time.monotonic() < deadline:
                if time.monotonic() >= next_telemetry:
                    sample = _telemetry_sample(board, zero)
                    if sample is None:
                        raise RuntimeError("Stationary telemetry observation failed")
                    require_unchanged_idle(sample, initial)
                    samples.append(sample)
                    next_telemetry = time.monotonic() + 0.05
                    if with_drivers and time.monotonic() >= next_driver:
                        for role, endpoint in AXES["rho"].driver_dump_paths:
                            started = time.monotonic() - zero
                            driver = board.get(endpoint + "?motionHealth=true")
                            ended = time.monotonic() - zero
                            if driver.get("snapshotKind") != "motion-health":
                                raise RuntimeError("Missing motion-health snapshot")
                            guard.observe(role, driver, "stationary-probe")
                            driver_samples.append({"role": role, "requestStartHostS": started,
                                                   "requestEndHostS": ended, "driver": driver})
                        next_driver = time.monotonic() + 0.10
                time.sleep(0.005)
            block["endHostS"] = time.monotonic() - zero
            print(json.dumps(block), flush=True)
        final = board.get("/api/motion/telemetry")
        require_unchanged_idle(final, initial)
        report["finalTelemetry"] = final
        report["gainAfter"] = microphone_metadata(args.source)
        if gain_before != report["gainAfter"]:
            raise RuntimeError("Microphone metadata changed during stationary probe")
        report["completed"] = True
    except BaseException as error:
        report["error"] = repr(error)
        raise
    finally:
        # No POSTs, settings changes, motion commands, or stop/origin mutation.
        try:
            recorder.stop()
        finally:
            path.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    analyze_report(path.with_suffix(".json"))


if __name__ == "__main__":
    main()
