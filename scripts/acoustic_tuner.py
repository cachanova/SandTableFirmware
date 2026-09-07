#!/usr/bin/env python3
"""Measure relative stepper noise while driving a Sisyphus tuning test.

This is deliberately a one-trial-at-a-time tool. It never increases motor
current on its own: each trial requires the motor's rated phase current.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import signal
import subprocess
import sys
import time
import wave
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np
import requests


DEFAULT_BOARD = "http://100.76.149.200"
DEFAULT_SOURCE = "alsa_input.usb-Antlion_Audio_Antlion_USB_Microphone-00.mono-fallback"
DEFAULT_RATE = 48000
THETA_CURRENT_CEILING_MA = 1500
HUMAN_AUDIBLE_MIN_HZ = 20.0
HUMAN_AUDIBLE_MAX_HZ = 20000.0
TONE_MATCH_TOLERANCE_HZ = 12.5
# The operator-accepted 0.25 rad/s reference at the fixed close-microphone
# position. Trials can override this to build quieter performance profiles.
DEFAULT_ACCEPTABLE_NEAR_HIGH_SPEED_CEILING_DBFS = -53.92
# Loudest repeatable motion-locked mechanism line in the accepted 0.48 rad/s
# reference.  Unlike the broadband level, this remains useful when unrelated
# room sound occurs during the motion window.  Override it for quieter tiers.
DEFAULT_ACCEPTABLE_NEAR_TONE_CEILING_DBFS = -58.45


@dataclass
class AcousticMetrics:
    duration_s: float
    sample_rate_hz: int
    peak_dbfs: float
    rms_dbfs: float
    a_weighted_dbfs: float
    motor_excess_dbfs: float | None
    motor_excess_over_baseline_db: float | None
    dominant_tones_hz: list[float]
    dominant_tone_levels_db: list[float]


@dataclass
class TimingLockedTone:
    frequency_hz: float
    motion_level_dbfs: float
    gain_over_pre_idle_db: float
    gain_over_post_idle_db: float
    motion_persistence: float
    pre_idle_presence: float
    post_idle_presence: float


@dataclass
class TimedAcousticMetrics:
    duration_s: float
    sample_rate_hz: int
    actual_motion_start_s: float
    actual_motion_end_s: float
    motion_duration_s: float
    audible_peak_dbfs: float
    audible_rms_dbfs: float
    pre_idle_a_weighted_dbfs: float
    post_idle_a_weighted_dbfs: float
    motion_a_weighted_dbfs: float
    motion_p95_a_weighted_dbfs: float
    idle_p95_a_weighted_dbfs: float
    motion_gain_over_idle_db: float
    transient_gain_over_idle_p95_db: float
    motion_broadband_detected: bool
    motion_transient_detected: bool
    background_drift_db: float
    background_stable: bool
    timing_locked_a_weighted_dbfs: float | None
    timing_locked_tones: list[TimingLockedTone]
    rejected_candidate_count: int
    rejected_candidates: list[dict[str, Any]]


def db(value: float, power: bool = False) -> float:
    return (10.0 if power else 20.0) * math.log10(max(float(value), 1e-20))


def read_wav(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        rate = wav.getframerate()
        width = wav.getsampwidth()
        frames = wav.readframes(wav.getnframes())
    if width != 2:
        raise ValueError(f"Expected 16-bit PCM, got {width * 8}-bit audio")
    audio = np.frombuffer(frames, dtype="<i2").astype(np.float64) / 32768.0
    if channels > 1:
        audio = audio.reshape(-1, channels).mean(axis=1)
    return audio, rate


def averaged_spectrum(audio: np.ndarray, rate: int) -> tuple[np.ndarray, np.ndarray]:
    audio = audio - np.mean(audio)
    frame_size = min(8192, 1 << max(8, int(math.log2(max(len(audio) // 8, 256)))))
    hop = frame_size // 2
    if len(audio) < frame_size:
        audio = np.pad(audio, (0, frame_size - len(audio)))
    window = np.hanning(frame_size)
    window_power = np.sum(window * window)
    spectra = []
    for start in range(0, len(audio) - frame_size + 1, hop):
        frame = audio[start : start + frame_size] * window
        frame_power = (np.abs(np.fft.rfft(frame)) ** 2) / max(frame_size * window_power, 1e-20)
        if len(frame_power) > 2:
            frame_power[1:-1] *= 2.0
        spectra.append(frame_power)
    # Median Welch-style averaging rejects transient speech, doors, and other
    # room sounds much better than a mean while retaining persistent motor tones.
    spectrum = np.median(spectra, axis=0)
    frequencies = np.fft.rfftfreq(frame_size, 1.0 / rate)
    return frequencies, spectrum


def short_window_spectrogram(
    audio: np.ndarray, rate: int, frame_size: int = 4096, hop: int = 1024
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Return frame-center times, frequencies, and short-window power spectra."""
    if len(audio) < frame_size:
        audio = np.pad(audio, (0, frame_size - len(audio)))
    audio = audio - np.mean(audio)
    window = np.hanning(frame_size)
    window_power = np.sum(window * window)
    starts = np.arange(0, len(audio) - frame_size + 1, hop, dtype=int)
    spectra = np.empty((len(starts), frame_size // 2 + 1), dtype=np.float64)
    for row, start in enumerate(starts):
        frame = audio[start : start + frame_size] * window
        power = (np.abs(np.fft.rfft(frame)) ** 2) / max(frame_size * window_power, 1e-20)
        if len(power) > 2:
            power[1:-1] *= 2.0
        spectra[row] = power
    centers = (starts + frame_size / 2.0) / rate
    frequencies = np.fft.rfftfreq(frame_size, 1.0 / rate)
    return centers, frequencies, spectra


def analyze_timed(
    path: Path,
    motion_start_s: float,
    motion_end_s: float,
    minimum_gain_db: float = 6.0,
    minimum_persistence: float = 0.65,
) -> tuple[TimedAcousticMetrics, dict[str, np.ndarray]]:
    """Find audible tones whose presence is locked to measured motor motion.

    The two idle windows come from the same continuous recording. A candidate
    must be absent in both idle windows, remain present in most motion frames,
    and exceed each idle window by ``minimum_gain_db``.
    """
    audio, rate = read_wav(path)
    times, frequencies, spectra = short_window_spectrogram(audio, rate)
    guard_s = max(0.15, 2.0 * (times[1] - times[0])) if len(times) > 1 else 0.15
    pre = times < motion_start_s - guard_s
    moving = (times > motion_start_s + guard_s) & (times < motion_end_s - guard_s)
    post = times > motion_end_s + guard_s
    if min(int(np.sum(pre)), int(np.sum(moving)), int(np.sum(post))) < 5:
        raise ValueError(
            "Timed recording needs at least five spectrogram frames in each idle/motion window"
        )

    audible = (
        (frequencies >= HUMAN_AUDIBLE_MIN_HZ)
        & (frequencies <= min(HUMAN_AUDIBLE_MAX_HZ, rate / 2.0))
    )
    pre_median = np.median(spectra[pre], axis=0)
    post_median = np.median(spectra[post], axis=0)
    pre_p95 = np.percentile(spectra[pre], 95, axis=0)
    post_p95 = np.percentile(spectra[post], 95, axis=0)
    motion_median = np.median(spectra[moving], axis=0)
    idle_ceiling = np.maximum(pre_p95, post_p95)
    gain_floor = 10.0 ** (minimum_gain_db / 10.0)

    candidate_indexes = np.where(
        audible
        & (motion_median >= pre_median * gain_floor)
        & (motion_median >= post_median * gain_floor)
    )[0]
    ordered = candidate_indexes[np.argsort(motion_median[candidate_indexes])[::-1]]
    accepted: list[TimingLockedTone] = []
    accepted_indexes: list[int] = []
    rejected = 0
    rejected_candidates: list[dict[str, Any]] = []
    for index in ordered:
        if index <= 0 or index >= len(frequencies) - 1:
            continue
        if motion_median[index] < max(motion_median[index - 1], motion_median[index + 1]):
            continue
        if any(abs(frequencies[index] - frequencies[other]) < 25.0 for other in accepted_indexes):
            continue
        # Count a frame as containing the line only when it clears the 95th
        # percentile of both idle windows by 3 dB. This makes speech clicks and
        # persistent room tones fail rather than inflate the motor score.
        presence_threshold = max(idle_ceiling[index] * 2.0, 1e-20)
        motion_presence = float(np.mean(spectra[moving, index] >= presence_threshold))
        pre_presence = float(np.mean(spectra[pre, index] >= presence_threshold))
        post_presence = float(np.mean(spectra[post, index] >= presence_threshold))
        if motion_presence < minimum_persistence or pre_presence > 0.05 or post_presence > 0.05:
            rejected += 1
            if len(rejected_candidates) < 8:
                rejected_candidates.append({
                    "frequencyHz": round(float(frequencies[index]), 2),
                    "motionLevelDbfs": round(db(float(motion_median[index]), power=True), 2),
                    "gainOverPreIdleDb": round(
                        db(float(motion_median[index] / max(pre_median[index], 1e-20)), power=True), 2
                    ),
                    "gainOverPostIdleDb": round(
                        db(float(motion_median[index] / max(post_median[index], 1e-20)), power=True), 2
                    ),
                    "motionPersistence": round(motion_presence, 3),
                    "rejectionReason": (
                        "not persistent for enough measured motion"
                        if motion_presence < minimum_persistence
                        else "present during an idle window"
                    ),
                })
            continue
        accepted_indexes.append(int(index))
        accepted.append(TimingLockedTone(
            frequency_hz=round(float(frequencies[index]), 2),
            motion_level_dbfs=round(db(float(motion_median[index]), power=True), 2),
            gain_over_pre_idle_db=round(
                db(float(motion_median[index] / max(pre_median[index], 1e-20)), power=True), 2
            ),
            gain_over_post_idle_db=round(
                db(float(motion_median[index] / max(post_median[index], 1e-20)), power=True), 2
            ),
            motion_persistence=round(motion_presence, 3),
            pre_idle_presence=round(pre_presence, 3),
            post_idle_presence=round(post_presence, 3),
        ))
        if len(accepted) == 8:
            break

    weights = a_weighting_power(frequencies)
    frame_a_power = np.sum(spectra[:, audible] * weights[audible], axis=1)
    pre_a_dbfs = db(float(np.median(frame_a_power[pre])), power=True)
    post_a_dbfs = db(float(np.median(frame_a_power[post])), power=True)
    motion_a_dbfs = db(float(np.median(frame_a_power[moving])), power=True)
    motion_p95_a_dbfs = db(float(np.percentile(frame_a_power[moving], 95)), power=True)
    idle_p95_a_dbfs = db(float(max(
        np.percentile(frame_a_power[pre], 95),
        np.percentile(frame_a_power[post], 95),
    )), power=True)
    idle_median_a_dbfs = max(pre_a_dbfs, post_a_dbfs)
    motion_gain_over_idle_db = motion_a_dbfs - idle_median_a_dbfs
    transient_gain_over_idle_p95_db = motion_p95_a_dbfs - idle_p95_a_dbfs
    background_drift_db = abs(pre_a_dbfs - post_a_dbfs)
    locked_power = 0.0
    for index in accepted_indexes:
        band = slice(max(0, index - 1), min(len(frequencies), index + 2))
        excess = np.maximum(
            motion_median[band] - np.maximum(pre_median[band], post_median[band]), 0.0
        )
        locked_power += float(np.sum(excess * weights[band]))
    centered = audio - np.mean(audio)
    metrics = TimedAcousticMetrics(
        duration_s=round(len(audio) / rate, 3),
        sample_rate_hz=rate,
        actual_motion_start_s=round(motion_start_s, 3),
        actual_motion_end_s=round(motion_end_s, 3),
        motion_duration_s=round(motion_end_s - motion_start_s, 3),
        audible_peak_dbfs=round(db(float(np.max(np.abs(audio)))), 2),
        audible_rms_dbfs=round(db(float(np.sqrt(np.mean(np.square(centered))))), 2),
        pre_idle_a_weighted_dbfs=round(pre_a_dbfs, 2),
        post_idle_a_weighted_dbfs=round(post_a_dbfs, 2),
        motion_a_weighted_dbfs=round(motion_a_dbfs, 2),
        motion_p95_a_weighted_dbfs=round(motion_p95_a_dbfs, 2),
        idle_p95_a_weighted_dbfs=round(idle_p95_a_dbfs, 2),
        motion_gain_over_idle_db=round(motion_gain_over_idle_db, 2),
        transient_gain_over_idle_p95_db=round(transient_gain_over_idle_p95_db, 2),
        motion_broadband_detected=motion_gain_over_idle_db >= 3.0,
        motion_transient_detected=transient_gain_over_idle_p95_db >= 6.0,
        background_drift_db=round(background_drift_db, 2),
        background_stable=background_drift_db <= 2.0,
        timing_locked_a_weighted_dbfs=(
            round(db(locked_power, power=True), 2) if accepted_indexes else None
        ),
        timing_locked_tones=accepted,
        rejected_candidate_count=rejected,
        rejected_candidates=rejected_candidates,
    )
    return metrics, {
        "audio": audio,
        "times": times,
        "frequencies": frequencies,
        "spectra": spectra,
    }


def high_speed_acoustic_level(
    path: Path,
    recorder_launch_offset_s: float,
    telemetry: list[dict[str, Any]],
    acceptable_ceiling_dbfs: float = DEFAULT_ACCEPTABLE_NEAR_HIGH_SPEED_CEILING_DBFS,
) -> dict[str, Any]:
    """Measure near-field level only while telemetry is near peak speed."""
    audio, rate = read_wav(path)
    times, frequencies, spectra = short_window_spectrogram(audio, rate)
    host_times = times + recorder_launch_offset_s
    telemetry_times = np.asarray([float(item["hostOffsetS"]) for item in telemetry])
    telemetry_speeds = np.asarray([
        abs(float(item["velocity"]["theta"])) for item in telemetry
    ])
    interpolated_speed = np.interp(host_times, telemetry_times, telemetry_speeds)
    max_speed = float(np.max(interpolated_speed))
    selected = interpolated_speed >= 0.9 * max_speed
    if max_speed <= 0.002 or int(np.sum(selected)) < 3:
        return {"valid": False, "reason": "insufficient high-speed audio frames"}

    audible = (
        (frequencies >= HUMAN_AUDIBLE_MIN_HZ)
        & (frequencies <= min(HUMAN_AUDIBLE_MAX_HZ, rate / 2.0))
    )
    weights = a_weighting_power(frequencies)
    frame_power = np.sum(spectra[:, audible] * weights[audible], axis=1)
    level = db(float(np.median(frame_power[selected])), power=True)
    return {
        "valid": True,
        "frameCount": int(np.sum(selected)),
        "selection": ">= 90% of maximum telemetry-measured speed",
        "minimumSelectedSpeedRadS": round(float(np.min(interpolated_speed[selected])), 4),
        "maximumMeasuredSpeedRadS": round(max_speed, 4),
        "medianAWeightedDbfs": round(level, 2),
        "acceptableCeilingAWeightedDbfs": acceptable_ceiling_dbfs,
        "marginBelowAcceptableCeilingDb": round(
            acceptable_ceiling_dbfs - level, 2
        ),
        "withinAcceptableReference": level <= acceptable_ceiling_dbfs,
    }


def a_weighting_power(frequencies: np.ndarray) -> np.ndarray:
    f2 = frequencies * frequencies
    numerator = (12194.0**2) * (f2**2)
    denominator = (
        (f2 + 20.6**2)
        * np.sqrt((f2 + 107.7**2) * (f2 + 737.9**2))
        * (f2 + 12194.0**2)
    )
    ratio = np.divide(numerator, denominator, out=np.zeros_like(frequencies), where=denominator > 0)
    return (ratio * (10.0 ** (2.0 / 20.0))) ** 2


def find_tones(frequencies: np.ndarray, excess: np.ndarray, count: int = 5) -> tuple[list[float], list[float]]:
    # Never use ultrasonic chopper content as a proxy for perceived motor
    # noise. These are only motion-minus-baseline candidates; repeatability is
    # evaluated separately before a candidate is called motor-associated.
    candidates = np.where(
        (frequencies >= HUMAN_AUDIBLE_MIN_HZ)
        & (frequencies <= HUMAN_AUDIBLE_MAX_HZ)
    )[0]
    ordered = candidates[np.argsort(excess[candidates])[::-1]]
    picked: list[int] = []
    for index in ordered:
        if excess[index] <= 0:
            break
        if all(abs(frequencies[index] - frequencies[other]) >= 25.0 for other in picked):
            picked.append(int(index))
        if len(picked) == count:
            break
    return (
        [round(float(frequencies[index]), 1) for index in picked],
        [round(db(float(excess[index]), power=True), 2) for index in picked],
    )


def analyze(path: Path, baseline_path: Path | None = None) -> AcousticMetrics:
    audio, rate = read_wav(path)
    frequencies, spectrum = averaged_spectrum(audio, rate)
    audible = (
        (frequencies >= HUMAN_AUDIBLE_MIN_HZ)
        & (frequencies <= min(HUMAN_AUDIBLE_MAX_HZ, rate / 2.0))
    )
    weights = a_weighting_power(frequencies)

    baseline_spectrum = np.zeros_like(spectrum)
    baseline_a_power = 0.0
    if baseline_path:
        baseline_audio, baseline_rate = read_wav(baseline_path)
        if baseline_rate != rate:
            raise ValueError("Baseline and motion recordings use different sample rates")
        baseline_freq, baseline_spectrum = averaged_spectrum(baseline_audio, baseline_rate)
        if len(baseline_freq) != len(frequencies):
            raise ValueError("Baseline and motion FFT sizes differ; use equal recording durations")
        baseline_a_power = float(np.sum(baseline_spectrum[audible] * weights[audible]))

    excess = np.maximum(spectrum - baseline_spectrum, 0.0)
    excess_a_power = float(np.sum(excess[audible] * weights[audible]))
    motion_a_power = float(np.sum(spectrum[audible] * weights[audible]))
    tones_hz, tone_levels = find_tones(frequencies, excess if baseline_path else spectrum)
    centered = audio - np.mean(audio)
    rms = float(np.sqrt(np.mean(np.square(centered))))
    peak = float(np.max(np.abs(audio))) if len(audio) else 0.0
    return AcousticMetrics(
        duration_s=round(len(audio) / rate, 3),
        sample_rate_hz=rate,
        peak_dbfs=round(db(peak), 2),
        rms_dbfs=round(db(rms), 2),
        a_weighted_dbfs=round(db(motion_a_power, power=True), 2),
        motor_excess_dbfs=round(db(excess_a_power, power=True), 2) if baseline_path else None,
        motor_excess_over_baseline_db=(
            round(db(excess_a_power / max(baseline_a_power, 1e-20), power=True), 2)
            if baseline_path else None
        ),
        dominant_tones_hz=tones_hz,
        dominant_tone_levels_db=tone_levels,
    )


def capture(path: Path, source: str, duration: float, rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([
        "pw-record", "--target", source, "--rate", str(rate), "--channels", "1",
        "--format", "s16", "--sample-count", str(round(rate * duration)), str(path),
    ], check=False)
    # PipeWire 1.6 may report status 1 when --sample-count ends an otherwise
    # successful capture. Validate the artifact instead of trusting that status.
    audio, captured_rate = read_wav(path)
    expected = round(rate * duration)
    if captured_rate != rate or len(audio) < expected * 0.98:
        raise RuntimeError(
            f"Incomplete microphone capture (status {result.returncode}, "
            f"{len(audio)}/{expected} samples)"
        )


class Board:
    def __init__(self, base_url: str) -> None:
        self.base_url = base_url.rstrip("/")

    def get(self, path: str) -> dict[str, Any]:
        response = requests.get(self.base_url + path, timeout=3)
        response.raise_for_status()
        return response.json()

    def post(self, path: str, data: dict[str, Any] | None = None) -> dict[str, Any]:
        response = requests.post(self.base_url + path, data=data, timeout=3)
        if not response.ok:
            try:
                message = response.json().get("message", response.text)
            except ValueError:
                message = response.text
            raise RuntimeError(f"Board rejected {path}: HTTP {response.status_code}: {message}")
        return response.json()

    def stop(self) -> None:
        self.post("/api/motion/stop")

    def wait_until_idle(self, timeout_s: float = 15.0) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            status = self.get("/api/status")
            if status.get("state") == "IDLE":
                return status
            time.sleep(0.1)
        raise RuntimeError("Board did not become IDLE after stop")

    def recovering_stop(self, timeout_s: float = 30.0) -> None:
        """Keep requesting stop through a transient Wi-Fi loss or board reboot."""
        deadline = time.monotonic() + timeout_s
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                self.stop()
                status = self.get("/api/status")
                if status.get("state") in {"IDLE", "INITIALIZED", "UNINITIALIZED"}:
                    return
            except requests.RequestException as error:
                last_error = error
            time.sleep(0.25)
        raise RuntimeError(f"Could not confirm stopped board after recovery window: {last_error}")


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def save_result(output_dir: Path, payload: dict[str, Any]) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    with (output_dir / "results.jsonl").open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(payload, sort_keys=True) + "\n")


def cmd_baseline(args: argparse.Namespace) -> int:
    board = Board(args.board)
    board.recovering_stop()
    time.sleep(args.settle)
    output_dir = Path(args.output_dir)
    path = output_dir / f"{utc_stamp()}-baseline.wav"
    capture(path, args.source, args.duration, args.rate)
    metrics = analyze(path)
    payload = {"kind": "baseline", "audio": str(path), "metrics": asdict(metrics)}
    save_result(output_dir, payload)
    print(json.dumps(payload, indent=2))
    return 0


def post_form(board: Board, path: str, values: dict[str, Any]) -> None:
    encoded = {
        key: ("true" if value is True else "false" if value is False else value)
        for key, value in values.items()
    }
    board.post(path, encoded)


def apply_requested_settings(board: Board, args: argparse.Namespace) -> dict[str, Any]:
    tuning = board.get("/api/tuning")
    motion = dict(tuning["motion"])
    theta = dict(tuning["thetaDriver"])

    motion_changes = {
        "tMaxVelocity": args.velocity,
        "tMaxAccel": args.accel,
        "tMaxJerk": args.jerk,
    }
    driver_changes = {
        "runCurrent": args.run_current_ma,
        "holdCurrent": args.hold_current_ma,
        "microsteps": args.microsteps,
        "stealthChopThreshold": args.stealth_threshold,
    }
    for key, value in motion_changes.items():
        if value is not None:
            motion[key] = value
    for key, value in driver_changes.items():
        if value is not None:
            theta[key] = value
    if args.mode:
        theta["stealthChopEnabled"] = args.mode == "stealthchop"
    if args.coolstep:
        theta["coolStepEnabled"] = args.coolstep == "on"

    requested_current = int(theta["runCurrent"])
    if requested_current > THETA_CURRENT_CEILING_MA:
        raise ValueError(
            f"Requested theta current {requested_current} mA exceeds the project ceiling "
            f"of {THETA_CURRENT_CEILING_MA} mA"
        )
    if requested_current > args.rated_current_ma:
        raise ValueError(
            f"Requested theta current {requested_current} mA exceeds the supplied motor rating "
            f"of {args.rated_current_ma} mA"
        )
    if int(theta["holdCurrent"]) > requested_current:
        raise ValueError("Hold current must not exceed run current")

    if any(value is not None for value in motion_changes.values()):
        post_form(board, "/api/tuning/motion", motion)
    if any(value is not None for value in driver_changes.values()) or args.mode or args.coolstep:
        post_form(board, "/api/tuning/theta", theta)
    return board.get("/api/tuning")


def microphone_metadata(source: str) -> dict[str, str]:
    metadata = {"source": source}
    try:
        listing = subprocess.run(
            ["pactl", "list", "sources"], check=True, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        ).stdout
        for block in re.split(r"(?=Source #\d+)", listing):
            if re.search(rf"^\s*Name:\s*{re.escape(source)}\s*$", block, re.MULTILINE):
                fields = {
                    "description": r"^\s*Description:\s*(.+)$",
                    "sampleSpecification": r"^\s*Sample Specification:\s*(.+)$",
                    "captureVolume": r"^\s*Volume:\s*(.+)$",
                    "baseVolume": r"^\s*Base Volume:\s*(.+)$",
                    "mute": r"^\s*Mute:\s*(.+)$",
                }
                for key, pattern in fields.items():
                    match = re.search(pattern, block, re.MULTILINE)
                    if match:
                        metadata[key] = match.group(1).strip()
                break
    except (OSError, subprocess.SubprocessError):
        metadata["metadataWarning"] = "Could not query PipeWire/PulseAudio source metadata"
    return metadata


def _telemetry_sample(board: Board, recording_zero: float) -> dict[str, Any] | None:
    try:
        sample = board.get("/api/motion/telemetry")
        sample["hostOffsetS"] = round(time.monotonic() - recording_zero, 6)
        return sample
    except requests.RequestException:
        return None


def save_timing_plot(
    path: Path,
    analysis: dict[str, np.ndarray],
    telemetry: list[dict[str, Any]],
    metrics: TimedAcousticMetrics,
) -> None:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return
    times = analysis["times"]
    frequencies = analysis["frequencies"]
    spectra = analysis["spectra"]
    audible = (
        (frequencies >= HUMAN_AUDIBLE_MIN_HZ)
        & (frequencies <= min(HUMAN_AUDIBLE_MAX_HZ, metrics.sample_rate_hz / 2.0))
    )
    spectral_db = 10.0 * np.log10(np.maximum(spectra[:, audible], 1e-20))
    vmax = float(np.percentile(spectral_db, 99.7))
    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    mesh = axes[0].pcolormesh(
        times, frequencies[audible], spectral_db.T, shading="auto",
        cmap="magma", vmin=vmax - 60.0, vmax=vmax,
    )
    axes[0].set_yscale("log")
    axes[0].set_ylim(HUMAN_AUDIBLE_MIN_HZ, HUMAN_AUDIBLE_MAX_HZ)
    axes[0].set_ylabel("Frequency (Hz)")
    axes[0].set_title("20 Hz–20 kHz short-window spectrogram")
    fig.colorbar(mesh, ax=axes[0], label="Power (dBFS/bin)")

    telemetry_times = [float(sample["hostOffsetS"]) for sample in telemetry]
    velocities = [float(sample["velocity"]["theta"]) for sample in telemetry]
    axes[1].plot(telemetry_times, velocities, color="#1874cd", linewidth=1.5)
    axes[1].axhline(0.0, color="black", linewidth=0.5)
    axes[1].set_ylabel("Theta velocity\n(rad/s)")
    axes[1].grid(alpha=0.25)

    for tone in metrics.timing_locked_tones[:5]:
        index = int(np.argmin(np.abs(frequencies - tone.frequency_hz)))
        axes[2].plot(
            times, 10.0 * np.log10(np.maximum(spectra[:, index], 1e-20)),
            label=f"{tone.frequency_hz:.1f} Hz",
        )
    if metrics.timing_locked_tones:
        axes[2].legend(loc="best", ncol=3)
    else:
        axes[2].text(
            0.5, 0.5, "No timing-locked tonal line accepted",
            ha="center", va="center", transform=axes[2].transAxes,
        )
    axes[2].set_ylabel("Tone power\n(dBFS/bin)")
    axes[2].set_xlabel("Seconds from recorder start")
    axes[2].grid(alpha=0.25)
    for axis in axes:
        axis.axvspan(
            metrics.actual_motion_start_s, metrics.actual_motion_end_s,
            color="#4caf50", alpha=0.10,
        )
        axis.axvline(metrics.actual_motion_start_s, color="#228b22", linestyle="--")
        axis.axvline(metrics.actual_motion_end_s, color="#b22222", linestyle="--")
    fig.suptitle("ESP32 telemetry-aligned acoustic validation")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def run_timed_repeat(
    board: Board,
    args: argparse.Namespace,
    output_dir: Path,
    prefix: str,
    profile: str,
) -> dict[str, Any]:
    board.recovering_stop()
    time.sleep(args.settle)
    audio_path = output_dir / f"{prefix}-{profile}-antlion-near.wav"
    plot_path = output_dir / f"{prefix}-{profile}-antlion-near-timing.png"
    timeline_path = output_dir / f"{prefix}-{profile}-timeline.json"
    output_dir.mkdir(parents=True, exist_ok=True)
    telemetry: list[dict[str, Any]] = []
    driver_samples: list[dict[str, Any]] = []
    gain_before = microphone_metadata(args.source)
    recording_zero = time.monotonic()
    recorder_launch_offset = time.monotonic() - recording_zero
    recorder_process = subprocess.Popen([
        "pw-record", "--target", args.source, "--rate", str(args.rate),
        "--channels", "1", "--format", "s16", "--latency", "10ms", str(audio_path),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    command_offset = 0.0
    stop_request_offset: float | None = None
    next_poll = recording_zero
    next_driver_poll = recording_zero

    def collect_for(duration_s: float) -> None:
        nonlocal next_poll, next_driver_poll
        deadline = time.monotonic() + duration_s
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_poll:
                sample = _telemetry_sample(board, recording_zero)
                if sample:
                    telemetry.append(sample)
                next_poll = now + args.telemetry_interval
            if now >= next_driver_poll:
                try:
                    driver = board.get("/api/tuning/dump/theta")
                    driver["hostOffsetS"] = round(time.monotonic() - recording_zero, 6)
                    driver_samples.append(driver)
                except requests.RequestException:
                    pass
                next_driver_poll = now + 0.5
            time.sleep(0.01)

    try:
        time.sleep(0.25)
        if recorder_process.poll() is not None:
            raise RuntimeError("The Antlion recorder exited before the trial began")
        collect_for(args.pre_idle)
        board.post(f"/api/tuning/test/theta/{profile}")
        command_offset = time.monotonic() - recording_zero
        motion_deadline = time.monotonic() + args.duration
        motion_seen = False
        consecutive_idle = 0
        while time.monotonic() < motion_deadline and consecutive_idle < 3:
            collect_for(args.telemetry_interval)
            if telemetry:
                last = telemetry[-1]
                moving = abs(float(last["velocity"]["theta"])) >= args.motion_velocity_threshold
                if moving or last.get("state") in ("RUNNING", "STOPPING"):
                    motion_seen = True
                is_idle = (
                    motion_seen and last.get("state") == "IDLE"
                    and not moving
                )
                consecutive_idle = consecutive_idle + 1 if is_idle else 0
        if not motion_seen:
            raise RuntimeError("ESP32 telemetry did not confirm theta motion")
        if consecutive_idle < 3:
            board.stop()
            stop_request_offset = time.monotonic() - recording_zero
            idle_deadline = time.monotonic() + args.stop_timeout
            while time.monotonic() < idle_deadline and consecutive_idle < 3:
                collect_for(args.telemetry_interval)
                if telemetry:
                    last = telemetry[-1]
                    is_idle = (
                        last.get("state") == "IDLE"
                        and abs(float(last["velocity"]["theta"]))
                        < args.motion_velocity_threshold
                    )
                    consecutive_idle = consecutive_idle + 1 if is_idle else 0
            raise RuntimeError(
                f"Theta {profile} test did not complete within {args.duration:.1f}s; "
                "the partial run was stopped and cannot qualify a profile"
            )
        collect_for(args.post_idle)
    finally:
        try:
            board.recovering_stop()
        finally:
            if recorder_process.poll() is None:
                recorder_process.send_signal(signal.SIGINT)
                try:
                    recorder_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    recorder_process.terminate()
                    recorder_process.wait(timeout=2)

    audio, captured_rate = read_wav(audio_path)
    gain_after = microphone_metadata(args.source)
    if captured_rate != args.rate or len(audio) < args.rate * (args.pre_idle + args.post_idle):
        raise RuntimeError("Antlion microphone recording is incomplete")
    moving_samples = [
        sample for sample in telemetry
        if abs(float(sample["velocity"]["theta"])) >= args.motion_velocity_threshold
    ]
    if len(moving_samples) < 5:
        raise RuntimeError("ESP32 telemetry did not confirm theta motion")
    motion_start = float(moving_samples[0]["hostOffsetS"])
    motion_end = float(moving_samples[-1]["hostOffsetS"])
    recording_motion_start = motion_start - recorder_launch_offset
    recording_motion_end = motion_end - recorder_launch_offset
    metrics, analysis = analyze_timed(
        audio_path, recording_motion_start, recording_motion_end,
        minimum_gain_db=args.minimum_gain_db,
        minimum_persistence=args.minimum_persistence,
    )
    near_high_speed = high_speed_acoustic_level(
        audio_path, recorder_launch_offset, telemetry, args.acceptable_ceiling_dbfs,
    )
    recording_telemetry = [
        {**sample, "hostOffsetS": float(sample["hostOffsetS"]) - recorder_launch_offset}
        for sample in telemetry
    ]
    save_timing_plot(plot_path, analysis, recording_telemetry, metrics)
    timeline = {
        "clock": "host monotonic seconds from capture orchestration start",
        "recorderLaunchOffsetS": round(recorder_launch_offset, 6),
        "commandOffsetS": round(command_offset, 6),
        "stopRequestOffsetS": (
            round(stop_request_offset, 6) if stop_request_offset is not None else None
        ),
        "testCompletedNaturally": True,
        "actualMotionStartS": round(motion_start, 6),
        "actualMotionEndS": round(motion_end, 6),
        "telemetry": telemetry,
        "driverSamples": driver_samples,
    }
    with timeline_path.open("w", encoding="utf-8") as stream:
        json.dump(timeline, stream, indent=2)
    return {
        "profile": profile,
        "audio": str(audio_path),
        "timingPlot": str(plot_path),
        "timeline": str(timeline_path),
        "metrics": asdict(metrics),
        "gainFingerprintBefore": gain_before,
        "gainFingerprintAfter": gain_after,
        "gainFingerprintStable": gain_before == gain_after,
        "nearHighSpeed": near_high_speed,
        "testCompletedNaturally": True,
        "telemetry": telemetry,
        "driverSamples": driver_samples,
    }


def confirmed_tones(repeats: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Keep only tonal lines independently accepted in every repeat."""
    if len(repeats) < 2:
        return []
    tone_sets = [item["metrics"]["timing_locked_tones"] for item in repeats]
    confirmed: list[dict[str, Any]] = []
    for seed in tone_sets[0]:
        matches = [seed]
        for tones in tone_sets[1:]:
            nearby = min(
                tones,
                key=lambda tone: abs(tone["frequency_hz"] - seed["frequency_hz"]),
                default=None,
            )
            if nearby is None or abs(nearby["frequency_hz"] - seed["frequency_hz"]) > TONE_MATCH_TOLERANCE_HZ:
                break
            matches.append(nearby)
        if len(matches) != len(tone_sets):
            continue
        levels = [float(tone["motion_level_dbfs"]) for tone in matches]
        confirmed.append({
            "frequencyHz": round(float(np.median([tone["frequency_hz"] for tone in matches])), 2),
            "medianMotionLevelDbfs": round(float(np.median(levels)), 2),
            "repeatLevelSpreadDb": round(max(levels) - min(levels), 2),
            "minimumPersistence": min(float(tone["motion_persistence"]) for tone in matches),
            "minimumIdleSeparationDb": min(
                min(float(tone["gain_over_pre_idle_db"]), float(tone["gain_over_post_idle_db"]))
                for tone in matches
            ),
        })
    return sorted(confirmed, key=lambda tone: tone["medianMotionLevelDbfs"], reverse=True)


def cmd_trial(args: argparse.Namespace) -> int:
    if args.repeats < 1 or args.repeats > 5:
        raise ValueError("--repeats must be between 1 and 5")
    if args.duration <= 0 or args.pre_idle <= 0 or args.post_idle <= 0:
        raise ValueError("--duration, --pre-idle, and --post-idle must be positive")
    if args.rated_current_ma <= 0:
        raise ValueError("--rated-current-ma must be positive")
    board = Board(args.board)
    board.recovering_stop()
    board.post("/api/speed", {"speed": 10})
    tuning = apply_requested_settings(board, args)
    theta = tuning["thetaDriver"]
    status = board.get("/api/status")
    if status.get("state") != "IDLE":
        raise RuntimeError(f"Board must be IDLE for a trial; state is {status.get('state')}")

    output_dir = Path(args.output_dir)
    stamp = utc_stamp()
    label = re.sub(r"[^A-Za-z0-9_.-]+", "-", args.label).strip("-")
    if not label:
        raise ValueError("--label must contain at least one filename-safe character")
    profiles = ["continuous", "stress"] if args.profile == "both" else [args.profile]
    repeat_results: list[dict[str, Any]] = []
    for profile in profiles:
        for repeat_index in range(1, args.repeats + 1):
            prefix = f"{stamp}-{label}-r{repeat_index}"
            print(f"[{profile} repeat {repeat_index}/{args.repeats}] recording idle -> motion -> idle", file=sys.stderr)
            repeat_results.append(run_timed_repeat(
                board, args, output_dir, prefix, profile,
            ))

    profile_summaries: dict[str, dict[str, Any]] = {}
    for profile in profiles:
        profile_repeats = [item for item in repeat_results if item["profile"] == profile]
        tones = confirmed_tones(profile_repeats)
        locked_levels = [
            item["metrics"]["timing_locked_a_weighted_dbfs"] for item in profile_repeats
            if item["metrics"]["timing_locked_a_weighted_dbfs"] is not None
        ]
        valid_detection = bool(tones) and all(
            item["gainFingerprintStable"] for item in profile_repeats
        )
        level_valid = valid_detection and all(
            item["metrics"]["background_stable"] for item in profile_repeats
        )
        near_high_speed_levels = [
            float(item["nearHighSpeed"]["medianAWeightedDbfs"])
            for item in profile_repeats
            if item["nearHighSpeed"].get("valid", False)
        ]
        repeat_tone_levels = [
            float(tone["motion_level_dbfs"])
            for item in profile_repeats
            for tone in item["metrics"]["timing_locked_tones"]
        ]
        confirmed_tone_levels = [
            float(tone["medianMotionLevelDbfs"])
            for tone in tones
        ]
        profile_summaries[profile] = {
            "nearField": {
                "microphone": "Antlion close to motor",
                "acceptedAsMotorNoise": valid_detection,
                "levelValidForComparison": level_valid,
                "confirmedTimingLockedTones": tones,
                "acceptableTimingLockedToneCeilingDbfs": args.tone_ceiling_dbfs,
                "timingLockedToneDetectedInEveryRepeat": all(
                    bool(item["metrics"]["timing_locked_tones"])
                    for item in profile_repeats
                ),
                "loudestDetectedTimingLockedToneDbfs": (
                    round(max(repeat_tone_levels), 2) if repeat_tone_levels else None
                ),
                "loudestConfirmedTimingLockedToneDbfs": (
                    round(max(confirmed_tone_levels), 2) if confirmed_tone_levels else None
                ),
                "allDetectedTimingLockedTonesWithinCeiling": (
                    bool(repeat_tone_levels)
                    and all(level <= args.tone_ceiling_dbfs for level in repeat_tone_levels)
                ),
                "motorNoiseLevelAWeightedDbfs": (
                    round(float(np.median(locked_levels)), 2)
                    if level_valid and locked_levels else None
                ),
                "allBackgroundWindowsStable": all(
                    item["metrics"]["background_stable"] for item in profile_repeats
                ),
                "gainFingerprintStable": all(
                    item["gainFingerprintStable"] for item in profile_repeats
                ),
                "highSpeedMedianAWeightedDbfs": (
                    round(float(np.median(near_high_speed_levels)), 2)
                    if near_high_speed_levels else None
                ),
                "loudestRepeatHighSpeedAWeightedDbfs": (
                    round(max(near_high_speed_levels), 2)
                    if near_high_speed_levels else None
                ),
                "acceptableHighSpeedCeilingAWeightedDbfs": (
                    args.acceptable_ceiling_dbfs
                ),
                "everyRepeatWithinAcceptableReference": (
                    bool(near_high_speed_levels)
                    and len(near_high_speed_levels) == len(profile_repeats)
                    and all(
                        level <= args.acceptable_ceiling_dbfs
                        for level in near_high_speed_levels
                    )
                ),
            },
            "humanAudibilityDecisionRequired": True,
            "levelUnit": "dBFS (relative; not dB SPL)",
            "acceptance": (
                f"present during >= {args.minimum_persistence:.0%} of measured motion, "
                f">= {args.minimum_gain_db:.1f} dB above both idle windows, "
                f"frequency repeated within {TONE_MATCH_TOLERANCE_HZ:.1f} Hz"
            ),
        }

    all_telemetry = [sample for item in repeat_results for sample in item["telemetry"]]
    all_drivers = [sample for item in repeat_results for sample in item["driverSamples"]]
    driver_diagnostics = board.get("/api/tuning/dump/theta")
    errors = board.get("/api/errors")
    theta_steps_per_radian = (200.0 * int(theta["microsteps"]) / (2.0 * math.pi)) * (60.0 / 16.0)
    commanded_step_rate = float(tuning["motion"]["tMaxVelocity"]) * theta_steps_per_radian
    payload = {
        "kind": "theta_timing_locked_trial",
        "label": args.label,
        "recordedAt": stamp,
        "microphone": microphone_metadata(args.source),
        "settings": {"motion": tuning["motion"], "thetaDriver": theta},
        "summary": {
            "repeatConfirmationSatisfied": args.repeats >= 2,
            "allTestsCompletedNaturally": all(
                item["testCompletedNaturally"] for item in repeat_results
            ),
            "byProfile": profile_summaries,
            "maxMeasuredThetaVelocity": round(max(
                (abs(float(sample["velocity"]["theta"])) for sample in all_telemetry), default=0.0
            ), 6),
            "rhoStationary": all(
                abs(float(sample["position"]["rho"])) < 0.001
                and abs(float(sample["velocity"]["rho"])) < 0.001
                for sample in all_telemetry
            ),
            "plannerHealthy": all(
                int(sample["planner"]["underruns"]) == 0
                and int(sample["planner"]["maxConsecutiveUnderruns"]) == 0
                for sample in all_telemetry
            ),
            "commandedThetaStepRateHz": round(commanded_step_rate, 1),
            "stepRateBudgetPercent": round(commanded_step_rate / 10000.0 * 100.0, 1),
            "allDriverUartResponsesValid": bool(all_drivers) and all(
                sample.get("uartResponseValid", False) for sample in all_drivers
            ),
            "allDriverInterpolationTo256Confirmed": bool(all_drivers) and all(
                sample.get("settings", {}).get("chopconfReadValid", False)
                and sample.get("settings", {}).get("interpolationTo256", False)
                for sample in all_drivers
            ),
        },
        "repeats": repeat_results,
        "driverDiagnostics": driver_diagnostics,
        "errors": errors,
    }
    save_result(output_dir, payload)
    result_path = output_dir / f"{stamp}-{label}-result.json"
    with result_path.open("w", encoding="utf-8") as stream:
        json.dump(payload, stream, indent=2)
    # The full telemetry is in the result artifact; stdout stays useful for
    # interactive tuning and automation.
    print(json.dumps({
        "result": str(result_path),
        "microphone": payload["microphone"],
        "settings": payload["settings"],
        "summary": payload["summary"],
        "artifacts": [{
            "profile": item["profile"], "audio": item["audio"],
            "timingPlot": item["timingPlot"],
            "timeline": item["timeline"],
        } for item in repeat_results],
    }, indent=2))
    return 0


def cmd_analyze(args: argparse.Namespace) -> int:
    metrics = analyze(Path(args.audio), Path(args.baseline) if args.baseline else None)
    print(json.dumps(asdict(metrics), indent=2))
    return 0


def cmd_self_test(_: argparse.Namespace) -> int:
    rate = 32000
    duration = 8.0
    motion_start = 2.0
    motion_end = 6.0
    samples = np.arange(round(rate * duration)) / rate
    rng = np.random.default_rng(42)
    audio = rng.normal(0.0, 0.0005, len(samples))
    # A constant 700 Hz room tone must be rejected. The 1250 Hz line is the
    # simulated motor: present only during the telemetry-defined motion window.
    audio += 0.002 * np.sin(2.0 * np.pi * 700.0 * samples)
    motor = (samples >= motion_start) & (samples <= motion_end)
    audio[motor] += 0.01 * np.sin(2.0 * np.pi * 1250.0 * samples[motor])
    # A short unrelated transient during motion must also be rejected.
    transient = (samples >= 3.0) & (samples < 3.08)
    audio[transient] += 0.03 * np.sin(2.0 * np.pi * 2100.0 * samples[transient])
    output_dir = Path("/tmp/sisyphus-acoustic-self-test")
    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / "idle-motion-idle.wav"
    pcm = np.clip(audio * 32768.0, -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(rate)
        wav.writeframes(pcm.tobytes())
    metrics, analysis = analyze_timed(path, motion_start, motion_end)
    tones = [tone.frequency_hz for tone in metrics.timing_locked_tones]
    if not tones or min(abs(tone - 1250.0) for tone in tones) > 8.0:
        raise RuntimeError(f"Timed self-test did not recover the 1250 Hz motor tone: {tones}")
    if any(abs(tone - 700.0) <= 12.5 or abs(tone - 2100.0) <= 12.5 for tone in tones):
        raise RuntimeError(f"Timed self-test accepted an idle tone or transient: {tones}")
    save_timing_plot(
        output_dir / "timing.png", analysis,
        [
            {"hostOffsetS": 0.0, "velocity": {"theta": 0.0}},
            {"hostOffsetS": motion_start, "velocity": {"theta": 0.1}},
            {"hostOffsetS": motion_end, "velocity": {"theta": 0.0}},
            {"hostOffsetS": duration, "velocity": {"theta": 0.0}},
        ],
        metrics,
    )
    print(json.dumps({"metrics": asdict(metrics), "plot": str(output_dir / "timing.png")}, indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    def add_capture_options(command: argparse.ArgumentParser) -> None:
        command.add_argument("--board", default=DEFAULT_BOARD)
        command.add_argument("--source", default=DEFAULT_SOURCE)
        command.add_argument(
            "--duration",
            type=float,
            default=5.0,
            help="capture duration, or maximum natural-completion time for a trial",
        )
        command.add_argument("--rate", type=int, default=DEFAULT_RATE)
        command.add_argument("--settle", type=float, default=1.0)
        command.add_argument("--output-dir", default="tuning-recordings")

    baseline = subparsers.add_parser("baseline", help="record a stationary baseline")
    add_capture_options(baseline)
    baseline.set_defaults(func=cmd_baseline)

    trial = subparsers.add_parser(
        "trial", help="run repeated telemetry-aligned idle -> theta motion -> idle recordings"
    )
    add_capture_options(trial)
    trial.set_defaults(duration=300.0)
    trial.add_argument("--label", required=True)
    trial.add_argument("--rated-current-ma", type=int, required=True)
    trial.add_argument("--profile", choices=["continuous", "stress", "both"], default="both")
    trial.add_argument("--repeats", type=int, default=3)
    trial.add_argument("--pre-idle", type=float, default=5.0)
    trial.add_argument("--post-idle", type=float, default=5.0)
    trial.add_argument("--stop-timeout", type=float, default=15.0)
    trial.add_argument("--telemetry-interval", type=float, default=0.05)
    trial.add_argument("--motion-velocity-threshold", type=float, default=0.002)
    trial.add_argument("--minimum-gain-db", type=float, default=6.0)
    trial.add_argument("--minimum-persistence", type=float, default=0.65)
    trial.add_argument(
        "--acceptable-ceiling-dbfs",
        type=float,
        default=DEFAULT_ACCEPTABLE_NEAR_HIGH_SPEED_CEILING_DBFS,
        help="maximum acceptable telemetry-selected Antlion level",
    )
    trial.add_argument(
        "--tone-ceiling-dbfs",
        type=float,
        default=DEFAULT_ACCEPTABLE_NEAR_TONE_CEILING_DBFS,
        help="maximum acceptable level for a timing-locked Antlion tone",
    )
    trial.add_argument("--run-current-ma", type=int)
    trial.add_argument("--hold-current-ma", type=int)
    trial.add_argument("--velocity", type=float, help="theta maximum velocity in rad/s")
    trial.add_argument("--accel", type=float, help="theta maximum acceleration in rad/s^2")
    trial.add_argument("--jerk", type=float, help="theta maximum jerk in rad/s^3")
    trial.add_argument("--microsteps", type=int, choices=[1, 2, 4, 8, 16, 32, 64, 128, 256])
    trial.add_argument("--mode", choices=["stealthchop", "spreadcycle"])
    trial.add_argument("--coolstep", choices=["on", "off"])
    trial.add_argument("--stealth-threshold", type=int)
    trial.set_defaults(func=cmd_trial)

    analyze_parser = subparsers.add_parser("analyze", help="analyze existing WAV files")
    analyze_parser.add_argument("audio")
    analyze_parser.add_argument("--baseline")
    analyze_parser.set_defaults(func=cmd_analyze)

    self_test = subparsers.add_parser("self-test", help="verify analysis with a synthetic tone")
    self_test.set_defaults(func=cmd_self_test)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return int(args.func(args))
    except (OSError, ValueError, RuntimeError, requests.RequestException) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
