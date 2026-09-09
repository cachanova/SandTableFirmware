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
import threading
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
RHO_CURRENT_CEILING_MA = 500
RHO_TEST_MAX_EXCURSION_MM = 400.0
RHO_POSITION_TOLERANCE_MM = 0.05
RHO_PROFILE_DISTANCE_MM = {"continuous": 800.0, "stress": 5950.0}
RHO_SCREEN_GATE_COUNT = 4
RHO_QUALIFICATION_GATE_COUNT = 8
MINIMUM_GATE_CRUISE_S = 1.0
HUMAN_AUDIBLE_MIN_HZ = 20.0
HUMAN_AUDIBLE_MAX_HZ = 20000.0
TONE_MATCH_TOLERANCE_HZ = 12.5
TONE_MIN_PROMINENCE_DB = 6.0
TELEMETRY_MAX_P95_GAP_S = 0.15
TELEMETRY_MAX_GAP_S = 0.50
TELEMETRY_MAX_TRANSITION_BRACKET_S = 0.25
AUDIO_MAX_DURATION_SKEW_S = 0.10
# The operator-accepted 0.25 rad/s reference at the fixed close-microphone
# position. Trials can override this to build quieter performance profiles.
DEFAULT_ACCEPTABLE_NEAR_HIGH_SPEED_CEILING_DBFS = -53.92
# Loudest repeatable motion-locked mechanism line in the accepted 0.48 rad/s
# reference.  Unlike the broadband level, this remains useful when unrelated
# room sound occurs during the motion window.  Override it for quieter tiers.
DEFAULT_ACCEPTABLE_NEAR_TONE_CEILING_DBFS = -58.45


def rho_segment_targets(profile: str, excursion_mm: float) -> tuple[float, ...]:
    gate_count = (
        RHO_SCREEN_GATE_COUNT if profile == "screen" else RHO_QUALIFICATION_GATE_COUNT
    )
    return tuple(
        excursion_mm if index % 2 == 0 else 0.0
        for index in range(gate_count)
    )


def rho_profile_distance_mm(profile: str, excursion_mm: float) -> float:
    if profile in ("screen", "gated"):
        return len(rho_segment_targets(profile, excursion_mm)) * excursion_mm
    return RHO_PROFILE_DISTANCE_MM[profile]


@dataclass(frozen=True)
class AxisConfig:
    name: str
    other_axis: str
    driver_key: str
    motion_velocity_key: str
    motion_accel_key: str
    motion_jerk_key: str
    velocity_unit: str
    current_ceiling_ma: int
    tuning_path: str
    test_path_prefix: str
    driver_dump_paths: tuple[tuple[str, str], ...]


AXES = {
    "theta": AxisConfig(
        name="theta",
        other_axis="rho",
        driver_key="thetaDriver",
        motion_velocity_key="tMaxVelocity",
        motion_accel_key="tMaxAccel",
        motion_jerk_key="tMaxJerk",
        velocity_unit="rad/s",
        current_ceiling_ma=THETA_CURRENT_CEILING_MA,
        tuning_path="/api/tuning/theta",
        test_path_prefix="/api/tuning/test/theta",
        driver_dump_paths=(("theta", "/api/tuning/dump/theta"),),
    ),
    "rho": AxisConfig(
        name="rho",
        other_axis="theta",
        driver_key="rhoDriver",
        motion_velocity_key="rMaxVelocity",
        motion_accel_key="rMaxAccel",
        motion_jerk_key="rMaxJerk",
        velocity_unit="mm/s",
        current_ceiling_ma=RHO_CURRENT_CEILING_MA,
        tuning_path="/api/tuning/rho",
        test_path_prefix="/api/tuning/test/rho",
        driver_dump_paths=(
            ("rho", "/api/tuning/dump/rho"),
            ("rhoCompanion", "/api/tuning/dump/rho-companion"),
        ),
    ),
}


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
    direction: str
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
    clipping_detected: bool
    pre_idle_a_weighted_dbfs: float
    post_idle_a_weighted_dbfs: float
    motion_a_weighted_dbfs: float
    motion_p95_a_weighted_dbfs: float
    idle_p95_a_weighted_dbfs: float
    motion_gain_over_idle_db: float
    transient_gain_over_idle_p95_db: float
    motion_broadband_detected: bool
    motion_transient_detected: bool
    broadband_excess_a_weighted_dbfs: float | None
    broadband_gate_excess_a_weighted_dbfs: list[float]
    loudest_broadband_gate_excess_a_weighted_dbfs: float | None
    outbound_broadband_excess_a_weighted_dbfs: float | None
    inbound_broadband_excess_a_weighted_dbfs: float | None
    gate_peak_velocities: list[float]
    gate_cruise_durations_s: list[float]
    all_gates_sustain_commanded_velocity: bool | None
    broadband_persistence: float
    on_off_pair_count: int
    on_off_consistent_count: int
    on_off_consistency: float | None
    background_drift_db: float
    background_stable: bool
    timing_locked_a_weighted_dbfs: float | None
    timing_locked_tones: list[TimingLockedTone]
    rejected_candidate_count: int
    rejected_candidates: list[dict[str, Any]]


def db(value: float, power: bool = False) -> float:
    return (10.0 if power else 20.0) * math.log10(max(float(value), 1e-20))


def driver_current_quantization(
    requested_ma: int, high_sensitivity: bool, sense_resistor_ohms: float
) -> tuple[int, float]:
    """Return the TMC2209 CS code and nominal RMS current it commands."""
    vsense_v = 0.180 if high_sensitivity else 0.325
    full_scale_ma = (
        1000.0 * vsense_v / (sense_resistor_ohms + 0.020) / math.sqrt(2.0)
    )
    raw_code = requested_ma / full_scale_ma * 32.0 - 1.0
    current_code = max(0, min(31, math.floor(raw_code + 0.5)))
    actual_current_ma = (current_code + 1.0) / 32.0 * full_scale_ma
    return current_code, actual_current_ma


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
    telemetry: list[dict[str, Any]] | None = None,
    recorder_launch_offset_s: float = 0.0,
    axis: AxisConfig | None = None,
    velocity_threshold: float = 0.002,
    audio_epoch_uncertainty_s: float = 0.0,
    expected_max_velocity: float | None = None,
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
    post = times > motion_end_s + guard_s
    moving = (times > motion_start_s + guard_s) & (times < motion_end_s - guard_s)
    direction_windows: list[tuple[str, np.ndarray]] = []
    confirmed_idle: np.ndarray | None = None
    velocities: np.ndarray | None = None
    if telemetry is not None and axis is not None:
        host_times = times + recorder_launch_offset_s
        telemetry_times = np.asarray([
            float(item["hostOffsetS"]) for item in telemetry
        ])
        velocities = np.interp(
            host_times, telemetry_times,
            [float(item["velocity"][axis.name]) for item in telemetry],
        )
        moving &= np.abs(velocities) >= velocity_threshold
        telemetry_velocities = np.asarray([
            float(item["velocity"][axis.name]) for item in telemetry
        ])
        telemetry_moving = np.abs(telemetry_velocities) >= velocity_threshold
        telemetry_direction = np.sign(telemetry_velocities)
        transition_indexes = np.flatnonzero(
            (telemetry_moving[1:] != telemetry_moving[:-1])
            | (
                telemetry_moving[1:]
                & telemetry_moving[:-1]
                & (telemetry_direction[1:] != telemetry_direction[:-1])
            )
        )
        frame_half_width_s = 4096.0 / (2.0 * rate) + audio_epoch_uncertainty_s
        transition_safe = np.ones(len(times), dtype=bool)
        for index in transition_indexes:
            transition_safe &= ~(
                (host_times + frame_half_width_s >= telemetry_times[index])
                & (host_times - frame_half_width_s <= telemetry_times[index + 1])
            )
        moving &= transition_safe
        nearest = np.searchsorted(telemetry_times, host_times, side="left")
        nearest = np.clip(nearest, 0, len(telemetry_times) - 1)
        previous = np.maximum(nearest - 1, 0)
        choose_previous = (
            np.abs(host_times - telemetry_times[previous])
            < np.abs(host_times - telemetry_times[nearest])
        )
        nearest[choose_previous] = previous[choose_previous]
        confirmed_idle = (
            np.abs(velocities) < velocity_threshold
        ) & np.asarray([
            telemetry[int(index)].get("state") == "IDLE" for index in nearest
        ]) & transition_safe
        idle_indexes = np.flatnonzero(confirmed_idle)
        if len(idle_indexes) >= 10:
            split_time = float(np.median(times[idle_indexes]))
            pre = confirmed_idle & (times <= split_time)
            post = confirmed_idle & (times > split_time)
        direction_windows = [
            ("outbound", moving & (velocities >= velocity_threshold)),
            ("inbound", moving & (velocities <= -velocity_threshold)),
        ]
    if min(int(np.sum(pre)), int(np.sum(moving)), int(np.sum(post))) < 5:
        raise ValueError(
            "Timed recording needs at least five spectrogram frames in each idle/motion window"
        )

    sustained_moving = moving.copy()
    sustained_direction_windows = direction_windows
    if velocities is not None and expected_max_velocity is not None:
        sustained_moving &= np.abs(velocities) >= 0.9 * expected_max_velocity
        sustained_direction_windows = [
            (
                "outbound",
                sustained_moving & (velocities >= 0.9 * expected_max_velocity),
            ),
            (
                "inbound",
                sustained_moving & (velocities <= -0.9 * expected_max_velocity),
            ),
        ]

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

    gate_windows: list[dict[str, Any]] = []
    if confirmed_idle is not None:
        moving_edges = np.diff(np.r_[False, moving, False].astype(np.int8))
        starts = np.flatnonzero(moving_edges == 1)
        stops = np.flatnonzero(moving_edges == -1)
        for start, stop in zip(starts, stops, strict=True):
            if stop - start < 5:
                continue
            before = confirmed_idle & (
                (times >= times[start] - 3.0) & (times < times[start])
            )
            after = confirmed_idle & (
                (times > times[stop - 1]) & (times <= times[stop - 1] + 3.0)
            )
            if min(int(np.sum(before)), int(np.sum(after))) < 3:
                continue
            direction = "both"
            if velocities is not None:
                direction = (
                    "outbound"
                    if float(np.median(velocities[start:stop])) >= 0.0
                    else "inbound"
                )
            gate_windows.append({
                "start": int(start), "stop": int(stop),
                "before": before, "after": after, "direction": direction,
            })

    accepted: list[TimingLockedTone] = []
    accepted_indexes: list[int] = []
    rejected = 0
    rejected_candidates: list[dict[str, Any]] = []
    # Tonal acceptance uses the same sustained-cruise frames as broadband
    # acceptance. Otherwise a longer acceleration ramp can dilute a line and
    # make an unchanged cruise condition appear quieter.
    detection_windows = [("both", sustained_moving), *sustained_direction_windows]
    for direction, detection_window in detection_windows:
        if int(np.sum(detection_window)) < 5:
            continue
        window_median = np.median(spectra[detection_window], axis=0)
        candidate_indexes = np.where(
            audible
            & (window_median >= pre_median * gain_floor)
            & (window_median >= post_median * gain_floor)
        )[0]
        ordered = candidate_indexes[np.argsort(window_median[candidate_indexes])[::-1]]
        for index in ordered:
            if index <= 0 or index >= len(frequencies) - 1:
                continue
            if window_median[index] < max(
                window_median[index - 1], window_median[index + 1]
            ):
                continue
            neighborhood = window_median[max(0, index - 6):min(len(frequencies), index + 7)]
            local_floor = float(np.median(neighborhood))
            if db(window_median[index] / max(local_floor, 1e-20), power=True) \
                    < TONE_MIN_PROMINENCE_DB:
                continue
            if any(abs(frequencies[index] - frequencies[other]) < 25.0
                   for other in accepted_indexes):
                continue
            presence_threshold = max(idle_ceiling[index] * 2.0, 1e-20)
            motion_presence = float(np.mean(
                spectra[detection_window, index] >= presence_threshold
            ))
            gate_consistency: float | None = None
            if gate_windows:
                relevant_gates = [
                    gate for gate in gate_windows
                    if direction == "both" or gate["direction"] == direction
                ]
                gate_presence: list[float] = []
                gate_consistent = 0
                for gate in relevant_gates:
                    start = int(gate["start"])
                    stop = int(gate["stop"])
                    local_idle_ceiling = max(
                        float(np.percentile(spectra[gate["before"], index], 95)),
                        float(np.percentile(spectra[gate["after"], index], 95)),
                    )
                    local_motion_values = spectra[start:stop, index]
                    if velocities is not None and expected_max_velocity is not None:
                        gate_cruise = (
                            np.abs(velocities[start:stop])
                            >= 0.9 * expected_max_velocity
                        )
                        local_motion_values = local_motion_values[gate_cruise]
                    if len(local_motion_values) < 5:
                        gate_presence.append(0.0)
                        continue
                    local_presence = float(np.mean(
                        local_motion_values >= 2.0 * max(local_idle_ceiling, 1e-20)
                    ))
                    gate_presence.append(local_presence)
                    local_motion_median = float(np.median(local_motion_values))
                    local_gate_spectra = spectra[start:stop]
                    if velocities is not None and expected_max_velocity is not None:
                        local_gate_spectra = local_gate_spectra[gate_cruise]
                    local_gate_spectrum = np.median(local_gate_spectra, axis=0)
                    local_neighborhood = local_gate_spectrum[
                        max(0, index - 6):min(len(frequencies), index + 7)
                    ]
                    local_prominence_db = db(
                        local_motion_median
                        / max(float(np.median(local_neighborhood)), 1e-20),
                        power=True,
                    )
                    if (
                        local_presence >= minimum_persistence
                        and local_prominence_db >= TONE_MIN_PROMINENCE_DB
                        and db(
                            local_motion_median / max(local_idle_ceiling, 1e-20),
                            power=True,
                        ) >= minimum_gain_db
                    ):
                        gate_consistent += 1
                if relevant_gates:
                    gate_consistency = gate_consistent / len(relevant_gates)
                    motion_presence = float(np.mean(gate_presence))
            pre_presence = float(np.mean(spectra[pre, index] >= presence_threshold))
            post_presence = float(np.mean(spectra[post, index] >= presence_threshold))
            if (motion_presence < minimum_persistence or pre_presence > 0.05
                    or post_presence > 0.05
                    or (gate_consistency is not None and gate_consistency < 0.75)):
                rejected += 1
                if len(rejected_candidates) < 8:
                    rejected_candidates.append({
                        "direction": direction,
                        "frequencyHz": round(float(frequencies[index]), 2),
                        "motionLevelDbfs": round(
                            db(float(window_median[index]), power=True), 2
                        ),
                        "motionPersistence": round(motion_presence, 3),
                        "rejectionReason": (
                            "not correlated across enough adjacent-idle gates"
                            if gate_consistency is not None and gate_consistency < 0.75
                            else (
                                "not persistent for enough measured motion"
                                if motion_presence < minimum_persistence
                                else "present during an idle window"
                            )
                        ),
                    })
                continue
            accepted_indexes.append(int(index))
            accepted.append(TimingLockedTone(
                direction=direction,
                frequency_hz=round(float(frequencies[index]), 2),
                motion_level_dbfs=round(db(float(window_median[index]), power=True), 2),
                gain_over_pre_idle_db=round(
                    db(float(window_median[index] / max(pre_median[index], 1e-20)), power=True), 2
                ),
                gain_over_post_idle_db=round(
                    db(float(window_median[index] / max(post_median[index], 1e-20)), power=True), 2
                ),
                motion_persistence=round(motion_presence, 3),
                pre_idle_presence=round(pre_presence, 3),
                post_idle_presence=round(post_presence, 3),
            ))
            if len(accepted) == 8:
                break
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
    background_spectrum = np.maximum(pre_median, post_median)
    broadband_excess_spectrum = np.maximum(motion_median - background_spectrum, 0.0)
    broadband_excess_power = float(np.sum(
        broadband_excess_spectrum[audible] * weights[audible]
    ))
    broadband_persistence = float(np.mean(
        frame_a_power[moving] >= 2.0 * max(
            np.median(frame_a_power[pre]),
            np.median(frame_a_power[post]),
        )
    ))
    on_off_pair_count = 0
    on_off_consistent_count = 0
    local_excess_powers: list[float] = []
    outbound_excess_powers: list[float] = []
    inbound_excess_powers: list[float] = []
    local_gain_db: list[float] = []
    local_persistent_frames = 0
    local_motion_frames = 0
    gate_peak_velocities: list[float] = []
    gate_cruise_durations_s: list[float] = []
    frame_period_s = float(np.median(np.diff(times)))
    for gate in gate_windows:
        start = int(gate["start"])
        stop = int(gate["stop"])
        before = gate["before"]
        after = gate["after"]
        local_idle = max(
            float(np.median(frame_a_power[before])),
            float(np.median(frame_a_power[after])),
        )
        local_frames = frame_a_power[start:stop]
        if velocities is not None and expected_max_velocity is not None:
            gate_speeds = np.abs(velocities[start:stop])
            gate_peak_velocities.append(round(float(np.max(gate_speeds)), 6))
            cruise = gate_speeds >= 0.9 * expected_max_velocity
            gate_cruise_durations_s.append(round(
                float(np.sum(cruise)) * frame_period_s, 3,
            ))
            local_frames = local_frames[cruise]
        if len(local_frames) == 0:
            local_motion = 0.0
        else:
            local_motion = float(np.median(local_frames))
        local_excess_power = max(local_motion - local_idle, 0.0)
        local_excess_powers.append(local_excess_power)
        if gate["direction"] == "outbound":
            outbound_excess_powers.append(local_excess_power)
        else:
            inbound_excess_powers.append(local_excess_power)
        segment_gain_db = db(
            local_motion / max(local_idle, 1e-20), power=True
        )
        local_gain_db.append(segment_gain_db)
        local_persistent_frames += int(np.sum(local_frames >= 2.0 * local_idle))
        local_motion_frames += len(local_frames)
        on_off_pair_count += 1
        if segment_gain_db >= 3.0:
            on_off_consistent_count += 1
    if local_excess_powers:
        # Each sustained-cruise window is compared only with the idle
        # immediately around its movement gate. The median remains useful as a
        # diagnostic; qualification separately uses the loudest gate.
        broadband_excess_power = float(np.median(local_excess_powers))
        motion_gain_over_idle_db = float(np.median(local_gain_db))
        broadband_persistence = (
            local_persistent_frames / local_motion_frames
            if local_motion_frames else 0.0
        )
    on_off_consistency = (
        on_off_consistent_count / on_off_pair_count if on_off_pair_count else None
    )
    all_gates_sustain_commanded_velocity: bool | None = None
    if velocities is not None and gate_windows and expected_max_velocity is not None:
        all_gates_sustain_commanded_velocity = all(
            peak >= 0.9 * expected_max_velocity
            and duration >= MINIMUM_GATE_CRUISE_S
            for peak, duration in zip(
                gate_peak_velocities, gate_cruise_durations_s, strict=True
            )
        )
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
        clipping_detected=bool(np.max(np.abs(audio)) >= 0.99),
        pre_idle_a_weighted_dbfs=round(pre_a_dbfs, 2),
        post_idle_a_weighted_dbfs=round(post_a_dbfs, 2),
        motion_a_weighted_dbfs=round(motion_a_dbfs, 2),
        motion_p95_a_weighted_dbfs=round(motion_p95_a_dbfs, 2),
        idle_p95_a_weighted_dbfs=round(idle_p95_a_dbfs, 2),
        motion_gain_over_idle_db=round(motion_gain_over_idle_db, 2),
        transient_gain_over_idle_p95_db=round(transient_gain_over_idle_p95_db, 2),
        motion_broadband_detected=motion_gain_over_idle_db >= 3.0,
        motion_transient_detected=transient_gain_over_idle_p95_db >= 6.0,
        broadband_excess_a_weighted_dbfs=(
            round(db(broadband_excess_power, power=True), 2)
            if motion_gain_over_idle_db >= 3.0 else None
        ),
        broadband_gate_excess_a_weighted_dbfs=[
            round(db(value, power=True), 2) for value in local_excess_powers
        ],
        loudest_broadband_gate_excess_a_weighted_dbfs=(
            round(db(max(local_excess_powers), power=True), 2)
            if local_excess_powers else None
        ),
        outbound_broadband_excess_a_weighted_dbfs=(
            round(db(float(np.median(outbound_excess_powers)), power=True), 2)
            if outbound_excess_powers else None
        ),
        inbound_broadband_excess_a_weighted_dbfs=(
            round(db(float(np.median(inbound_excess_powers)), power=True), 2)
            if inbound_excess_powers else None
        ),
        gate_peak_velocities=gate_peak_velocities,
        gate_cruise_durations_s=gate_cruise_durations_s,
        all_gates_sustain_commanded_velocity=all_gates_sustain_commanded_velocity,
        broadband_persistence=round(broadband_persistence, 3),
        on_off_pair_count=on_off_pair_count,
        on_off_consistent_count=on_off_consistent_count,
        on_off_consistency=(
            round(on_off_consistency, 3) if on_off_consistency is not None else None
        ),
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
    axis: AxisConfig,
    acceptable_ceiling_dbfs: float | None = DEFAULT_ACCEPTABLE_NEAR_HIGH_SPEED_CEILING_DBFS,
    *,
    background_stable: bool = True,
    timing_valid: bool = True,
    gain_stable: bool = True,
    clipping_detected: bool = False,
) -> dict[str, Any]:
    """Measure near-field level only while telemetry is near peak speed."""
    invalid_reasons = []
    if not background_stable:
        invalid_reasons.append("pre/post background drift exceeds limit")
    if not timing_valid:
        invalid_reasons.append("telemetry timing quality failed")
    if not gain_stable:
        invalid_reasons.append("microphone gain fingerprint changed")
    if clipping_detected:
        invalid_reasons.append("audio clipping detected")
    if invalid_reasons:
        return {"valid": False, "invalidReasons": invalid_reasons}
    audio, rate = read_wav(path)
    times, frequencies, spectra = short_window_spectrogram(audio, rate)
    host_times = times + recorder_launch_offset_s
    telemetry_times = np.asarray([float(item["hostOffsetS"]) for item in telemetry])
    telemetry_speeds = np.asarray([
        abs(float(item["velocity"][axis.name])) for item in telemetry
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
        "axis": axis.name,
        "speedUnit": axis.velocity_unit,
        "minimumSelectedSpeed": round(float(np.min(interpolated_speed[selected])), 4),
        "maximumMeasuredSpeed": round(max_speed, 4),
        "medianAWeightedDbfs": round(level, 2),
        "acceptableCeilingAWeightedDbfs": acceptable_ceiling_dbfs,
        "marginBelowAcceptableCeilingDb": (
            round(acceptable_ceiling_dbfs - level, 2)
            if acceptable_ceiling_dbfs is not None else None
        ),
        "withinAcceptableReference": (
            level <= acceptable_ceiling_dbfs
            if acceptable_ceiling_dbfs is not None else None
        ),
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


class TimestampedRecorder:
    """Capture raw PCM while establishing the host time of audio sample zero."""

    def __init__(self, path: Path, source: str, rate: int) -> None:
        self.path = path
        self.source = source
        self.rate = rate
        self.process: subprocess.Popen[bytes] | None = None
        self.thread: threading.Thread | None = None
        self.ready = threading.Event()
        self.audio_start_monotonic: float | None = None
        self.audio_end_monotonic: float | None = None
        self.timestamp_uncertainty_s = float("inf")
        self.priming_blocks_discarded = 0
        self.error: BaseException | None = None

    def start(self, timeout_s: float = 3.0) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.process = subprocess.Popen([
            "pw-record", "--raw", "--target", self.source,
            "--rate", str(self.rate), "--channels", "1", "--format", "s16",
            "--latency", "10ms", "-",
        ], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
        self.thread = threading.Thread(target=self._drain, name="antlion-recorder", daemon=True)
        self.thread.start()
        if not self.ready.wait(timeout_s):
            self.stop()
            raise RuntimeError("The Antlion recorder did not deliver audio within 3 seconds")
        if self.error is not None or self.audio_start_monotonic is None:
            self.stop()
            raise RuntimeError(f"The Antlion recorder failed during startup: {self.error}")

    def _drain(self) -> None:
        try:
            assert self.process is not None and self.process.stdout is not None
            with wave.open(str(self.path), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(self.rate)
                steady_priming_reads = 0
                priming_complete = False
                pipewire_block_bytes = 4096
                while True:
                    read_started_at = time.monotonic()
                    # PipeWire delivers this source in 4096-byte blocks. Once
                    # startup backlog is drained, each blocking read therefore
                    # advances at the corresponding audio cadence.
                    chunk = self.process.stdout.read(pipewire_block_bytes)
                    received_at = time.monotonic()
                    if not chunk:
                        break
                    if self.audio_start_monotonic is None:
                        block_duration_s = len(chunk) / (2.0 * self.rate)
                        read_duration_s = received_at - read_started_at
                        full_block = len(chunk) == pipewire_block_bytes
                        cadence_valid = (
                            full_block
                            and 0.5 * block_duration_s <= read_duration_s
                            <= 2.5 * block_duration_s
                        )
                        if not priming_complete:
                            # Drain any blocks queued during pw-record startup.
                            # Only a blocking read with audio-rate wall cadence
                            # demonstrates that the pipe backlog is gone.
                            steady_priming_reads = (
                                steady_priming_reads + 1 if cadence_valid else 0
                            )
                            self.priming_blocks_discarded += 1
                            if steady_priming_reads >= 3:
                                priming_complete = True
                            continue
                        if not cadence_valid:
                            # Scheduling or a renewed backlog invalidated the
                            # steady-state proof. Establish it again.
                            priming_complete = False
                            steady_priming_reads = 0
                            self.priming_blocks_discarded += 1
                            continue
                        # Bound sample zero using both sides of the first
                        # blocking read. The requested PipeWire latency is
                        # added to the half-width as a conservative allowance.
                        start_inferred_from_block_end = (
                            received_at - block_duration_s
                        )
                        self.audio_start_monotonic = (
                            read_started_at + start_inferred_from_block_end
                        ) / 2.0
                        self.timestamp_uncertainty_s = (
                            abs(start_inferred_from_block_end - read_started_at) / 2.0
                            + 0.010
                        )
                        self.ready.set()
                    self.audio_end_monotonic = received_at
                    wav.writeframesraw(chunk)
        except BaseException as error:  # Propagate recorder-thread failures to the trial.
            self.error = error
            self.ready.set()

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=2)
        if self.thread is not None:
            self.thread.join(timeout=5)
        if self.error is not None:
            raise RuntimeError(f"Antlion recorder failed: {self.error}")


class Board:
    def __init__(self, base_url: str) -> None:
        self.base_url = base_url.rstrip("/")
        self.session = requests.Session()

    def get(self, path: str, timeout_s: float = 3.0) -> dict[str, Any]:
        response = self.session.get(self.base_url + path, timeout=timeout_s)
        response.raise_for_status()
        return response.json()

    def post(self, path: str, data: dict[str, Any] | None = None) -> dict[str, Any]:
        response = self.session.post(self.base_url + path, data=data, timeout=3)
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


def driver_is_healthy(diagnostic: dict[str, Any]) -> bool:
    status = diagnostic.get("status", {})
    global_status = diagnostic.get("globalStatus", {})
    fault_fields = (
        "overTempWarning", "overTempShutdown", "shortToGroundA",
        "shortToGroundB", "lowSideShortA", "lowSideShortB",
        "overTemp120c", "overTemp143c", "overTemp150c", "overTemp157c",
    )
    return (
        diagnostic.get("uartResponseValid", False)
        and diagnostic.get("setupOk", False)
        and not any(bool(status.get(field, False)) for field in fault_fields)
        and not bool(global_status.get("drvErr", False))
        and not bool(global_status.get("uvCp", False))
    )


def preflight_rho_commissioning(
    board: Board, expected_interpolation: bool | None = None
) -> dict[str, Any]:
    """Require the guarded image, temporary origin, and both healthy drivers."""
    status = board.get("/api/status")
    if status.get("state") != "IDLE":
        raise RuntimeError(
            f"Board must be IDLE for a rho trial; state is {status.get('state')}"
        )
    telemetry = board.get("/api/motion/telemetry")
    if telemetry.get("commissioningAxis") != "rho":
        raise RuntimeError(
            "Rho trials require the rho-commissioning firmware; physical homing "
            "must not be bypassed in a production build"
        )
    logical_rho = float(telemetry["position"]["rho"])
    if abs(logical_rho) > RHO_POSITION_TOLERANCE_MM:
        raise RuntimeError(
            f"Rho must be at its temporary start (logical 0 mm), got {logical_rho:.3f} mm"
        )
    if abs(float(telemetry["velocity"]["rho"])) > 0.002:
        raise RuntimeError("Rho must be stationary before a trial")
    for driver_role, dump_path in AXES["rho"].driver_dump_paths:
        diagnostic = board.get(dump_path)
        if not driver_is_healthy(diagnostic):
            raise RuntimeError(f"{driver_role} driver failed commissioning preflight")
        verified_settings = diagnostic.get("settings", {})
        if not verified_settings.get("chopconfReadValid"):
            raise RuntimeError(
                f"{driver_role} driver CHOPCONF could not be verified"
            )
        if (
            expected_interpolation is not None
            and bool(verified_settings.get("interpolationTo256"))
            != expected_interpolation
        ):
            raise RuntimeError(
                f"{driver_role} driver did not confirm requested interpolation "
                f"state ({'on' if expected_interpolation else 'off'})"
            )
        if verified_settings.get("analogCurrentScaling") is not False:
            raise RuntimeError(
                f"{driver_role} driver did not confirm UART digital current scaling"
            )
        if verified_settings.get("internalSenseResistors") is not False:
            raise RuntimeError(
                f"{driver_role} driver did not confirm external sense resistors"
            )
        if not verified_settings.get("otpReadValid"):
            raise RuntimeError(f"{driver_role} driver OTP_READ could not be verified")
        if verified_settings.get("otpInternalSenseResistors") is not False:
            raise RuntimeError(
                f"{driver_role} OTP selects internal sensing; expected FYSETC V3.0 external shunts"
            )
    return telemetry


def apply_requested_settings(board: Board, args: argparse.Namespace) -> dict[str, Any]:
    axis = AXES[args.axis]
    tuning = board.get("/api/tuning")
    motion = dict(tuning["motion"])
    driver = dict(tuning[axis.driver_key])

    motion_changes = {
        axis.motion_velocity_key: args.velocity,
        axis.motion_accel_key: args.accel,
        axis.motion_jerk_key: args.jerk,
    }
    driver_changes = {
        "runCurrent": args.run_current_ma,
        "holdCurrent": args.hold_current_ma,
        "microsteps": args.microsteps,
        "stealthChopThreshold": args.stealth_threshold,
        "holdDelay": args.hold_delay,
        "powerDownDelay": args.power_down_delay,
        "chopperOffTime": args.chopper_off_time,
        "hysteresisStart": args.hysteresis_start,
        "hysteresisEnd": args.hysteresis_end,
        "blankTime": args.blank_time,
        "pwmFrequency": args.pwm_frequency,
        "pwmRegulation": args.pwm_regulation,
        "pwmLimit": args.pwm_limit,
        "standstillMode": args.standstill_mode,
        "pwmOffset": args.pwm_offset,
        "pwmGradient": args.pwm_gradient,
        "coolStepLowerThreshold": args.coolstep_lower,
        "coolStepUpperThreshold": args.coolstep_upper,
        "coolStepCurrentIncrement": args.coolstep_increment,
        "coolStepMeasurementCount": args.coolstep_samples,
        "coolStepThreshold": args.coolstep_threshold,
    }
    for key, value in motion_changes.items():
        if value is not None:
            motion[key] = value
    for key, value in driver_changes.items():
        if value is not None:
            driver[key] = value
    if args.mode:
        driver["stealthChopEnabled"] = args.mode == "stealthchop"
    if args.coolstep:
        driver["coolStepEnabled"] = args.coolstep == "on"
    if args.current_scale:
        driver["highSensitivityCurrentScale"] = args.current_scale == "high"
    if args.interpolation:
        driver["interpolationEnabled"] = args.interpolation == "on"
    if args.automatic_gradient:
        driver["automaticGradientAdaptation"] = args.automatic_gradient == "on"
    if args.automatic_current:
        driver["automaticCurrentScaling"] = args.automatic_current == "on"

    requested_current = int(driver["runCurrent"])
    if requested_current > axis.current_ceiling_ma:
        raise ValueError(
            f"Requested {axis.name} current {requested_current} mA exceeds the project ceiling "
            f"of {axis.current_ceiling_ma} mA"
        )
    if requested_current > args.rated_current_ma:
        raise ValueError(
            f"Requested {axis.name} current {requested_current} mA exceeds the supplied motor rating "
            f"of {args.rated_current_ma} mA"
        )
    limits = tuning.get("limits", {})
    sense_resistor_ohms = float(limits.get("driverSenseResistorOhms", 0.0))
    if sense_resistor_ohms <= 0.0:
        raise ValueError("Firmware did not report a valid driver sense resistance")
    current_code, actual_current_ma = driver_current_quantization(
        requested_current,
        bool(driver.get("highSensitivityCurrentScale", False)),
        sense_resistor_ohms,
    )
    tolerance_adjusted_current_ma = actual_current_ma * 1.06
    sense_verified = bool(limits.get("driverSenseResistorVerified", False))
    if axis.name == "rho" and not sense_verified:
        raise ValueError(
            "Rho commissioning requires a verified driver sense resistance"
        )
    if axis.name == "rho" and current_code > int(
        limits.get("rhoMaxUnmeasuredCurrentRegister", -1)
    ):
        raise ValueError(
            f"Requested {requested_current} mA quantizes to CS={current_code}; "
            "unmeasured FYSETC V3.0 operation is capped at CS=14"
        )
    if tolerance_adjusted_current_ma > min(axis.current_ceiling_ma, args.rated_current_ma):
        raise ValueError(
            f"Requested {requested_current} mA quantizes to {actual_current_ma:.0f} mA "
            f"nominal (CS={current_code}); including tolerance it exceeds the safe "
            "rating, so lower the request"
        )
    if int(driver["holdCurrent"]) > requested_current:
        raise ValueError("Hold current must not exceed run current")

    motion_requested = any(value is not None for value in motion_changes.values())
    driver_requested = (
        any(value is not None for value in driver_changes.values()) or args.mode
        or args.coolstep or args.current_scale or args.interpolation
        or args.automatic_gradient or args.automatic_current
    )
    if motion_requested and driver_requested:
        # Microsteps and maximum velocity jointly determine the STEP rate. Keep
        # the entire transition safe in both directions: lower velocity first,
        # change driver registers, then install the requested final velocity.
        transition_motion = dict(motion)
        transition_motion[axis.motion_velocity_key] = min(
            float(tuning["motion"][axis.motion_velocity_key]),
            float(motion[axis.motion_velocity_key]),
        )
        post_form(board, "/api/tuning/motion", transition_motion)
    elif motion_requested:
        post_form(board, "/api/tuning/motion", motion)
    if driver_requested:
        post_form(board, axis.tuning_path, driver)
    if motion_requested and driver_requested:
        post_form(board, "/api/tuning/motion", motion)
    return board.get("/api/tuning")


def precondition_rho_stealthchop(
    board: Board, velocity_mm_s: float, driver_settings: dict[str, Any],
    sense_resistor_ohms: float,
    excursion_mm: float = 100.0,
) -> dict[str, Any]:
    """Complete excluded AT#2 motion and return to the temporary origin."""
    irun_code, _ = driver_current_quantization(
        int(driver_settings["runCurrent"]),
        bool(driver_settings.get("highSensitivityCurrentScale", False)),
        sense_resistor_ohms,
    )
    automatic_stealth = (
        bool(driver_settings.get("stealthChopEnabled"))
        and bool(driver_settings.get("automaticCurrentScaling"))
        and bool(driver_settings.get("automaticGradientAdaptation"))
        and irun_code >= 8
    )
    validation: dict[str, Any] = {
        "required": automatic_stealth,
        "configuredIrunCode": irun_code,
        "notRequiredReason": (
            None if automatic_stealth else
            "IRUN below 8 or automatic StealthChop calibration disabled"
        ),
        "byDriver": {},
    }
    if not automatic_stealth:
        return {"motion": None, "samples": [], "at2Validation": validation}
    if automatic_stealth and velocity_mm_s < 4.0:
        raise RuntimeError(
            "StealthChop AT#2 requires at least 4 mm/s (60 motor RPM)"
        )
    deadline_per_leg_s = excursion_mm / max(velocity_mm_s, 0.1) * 1.5 + 10.0
    samples: list[dict[str, Any]] = []
    for target_mm in (excursion_mm, 0.0):
        board.post("/api/tuning/test/rho/segment", {"targetMm": target_mm})
        deadline = time.monotonic() + deadline_per_leg_s
        motion_seen = False
        while time.monotonic() < deadline:
            telemetry = board.get("/api/motion/telemetry")
            position = float(telemetry["position"]["rho"])
            velocity = abs(float(telemetry["velocity"]["rho"]))
            if position < -RHO_POSITION_TOLERANCE_MM:
                board.recovering_stop()
                raise RuntimeError("Rho preconditioning moved inward of temporary zero")
            motion_seen = motion_seen or velocity >= 0.002
            if velocity >= 0.9 * velocity_mm_s:
                for role, path in AXES["rho"].driver_dump_paths:
                    diagnostic = board.get(path)
                    if not driver_is_healthy(diagnostic):
                        raise RuntimeError(
                            f"{role} driver faulted during preconditioning"
                        )
                    samples.append({
                        "legTargetMm": target_mm,
                        "role": role,
                        "cruise": True,
                        "positionMm": round(position, 4),
                        "diagnostic": diagnostic,
                    })
            if (motion_seen and telemetry.get("state") == "IDLE" and velocity < 0.002
                    and abs(position - target_mm) <= RHO_POSITION_TOLERANCE_MM):
                break
            time.sleep(0.05)
        else:
            board.recovering_stop()
            raise RuntimeError("Rho StealthChop preconditioning did not complete")
        for role, path in AXES["rho"].driver_dump_paths:
            diagnostic = board.get(path)
            if not driver_is_healthy(diagnostic):
                raise RuntimeError(f"{role} driver faulted during preconditioning")
            samples.append({"legTargetMm": target_mm, "role": role,
                            "cruise": False,
                            "positionMm": round(target_mm, 4),
                            "diagnostic": diagnostic})
    final = board.get("/api/motion/telemetry")
    if abs(float(final["position"]["rho"])) > RHO_POSITION_TOLERANCE_MM:
        raise RuntimeError("Rho preconditioning did not return to temporary zero")
    if automatic_stealth:
        for role, _ in AXES["rho"].driver_dump_paths:
            role_samples = [
                item["diagnostic"] for item in samples
                if item["role"] == role
                and item.get("cruise") is True
            ]
            consecutive = 0
            maximum_consecutive = 0
            exact_zero_seen = False
            checked: list[dict[str, Any]] = []
            previous_gradient: int | None = None
            for diagnostic in role_samples:
                settings = diagnostic.get("settings", {})
                dynamic = diagnostic.get("dynamic", {})
                registers_valid = bool(dynamic.get("pwmScaleValid")) and bool(
                    dynamic.get("pwmAutoValid")
                )
                irun = int(settings.get("irunRegister", -1))
                offset = int(dynamic.get("pwmOffsetAuto", -1))
                scale_sum = int(dynamic.get("pwmScaleSum", -1))
                scale_auto = int(dynamic.get("pwmScaleAuto", 999))
                gradient = int(dynamic.get("pwmGradientAuto", -1))
                lower = 1.5 * offset * (irun + 1) / 32.0
                upper = 4.0 * offset * (irun + 1) / 32.0
                in_window = (
                    registers_valid and irun >= 0 and offset >= 0 and
                    lower < scale_sum < upper and scale_sum < 255
                )
                converged = in_window and abs(scale_auto) <= 1
                gradient_stable = previous_gradient is None or gradient == previous_gradient
                consecutive = consecutive + 1 if converged and gradient_stable else 0
                maximum_consecutive = max(maximum_consecutive, consecutive)
                exact_zero_seen = exact_zero_seen or (
                    in_window and scale_auto == 0
                )
                previous_gradient = gradient
                checked.append({
                    "irun": irun, "pwmOffsetAuto": offset,
                    "pwmScaleSum": scale_sum, "pwmScaleAuto": scale_auto,
                    "pwmGradientAuto": gradient,
                    "registersValid": registers_valid,
                    "requiredScaleSumLowerExclusive": round(lower, 3),
                    "requiredScaleSumUpperExclusive": round(upper, 3),
                    "inWindow": in_window,
                })
            passed = exact_zero_seen and maximum_consecutive >= 3
            validation["byDriver"][role] = {
                "passed": passed,
                "exactZeroSeen": exact_zero_seen,
                "maximumConsecutiveConvergedSamples": maximum_consecutive,
                "cruiseSampleCount": len(role_samples),
                "samples": checked,
            }
            if not passed:
                latest = checked[-1] if checked else None
                raise RuntimeError(
                    f"{role} StealthChop AT#2 did not converge during the excluded "
                    f"+{excursion_mm:g} mm -> 0 warm-up; "
                    f"cruiseSamples={len(role_samples)}, "
                    f"maxConsecutive={maximum_consecutive}, latest={latest}"
                )
    return {"motion": f"+{excursion_mm:g}mm -> 0mm", "samples": samples,
            "at2Validation": validation}


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


def _telemetry_sample(
    board: Board, recording_zero: float, timeout_s: float = 0.15
) -> dict[str, Any] | None:
    try:
        request_start = time.monotonic()
        # A late response has an ambiguous midpoint and can span a meaningful
        # part of a short gate. Drop it quickly and let timing validation judge
        # the resulting bracket instead of recording a misleading timestamp.
        sample = board.get("/api/motion/telemetry", timeout_s=timeout_s)
        request_end = time.monotonic()
        sample["hostOffsetS"] = round(
            ((request_start + request_end) / 2.0) - recording_zero, 6
        )
        sample["hostRequestRttS"] = round(request_end - request_start, 6)
        return sample
    except requests.RequestException:
        return None


def telemetry_timing_quality(
    telemetry: list[dict[str, Any]], axis: AxisConfig, velocity_threshold: float,
) -> dict[str, Any]:
    times = np.asarray([float(sample["hostOffsetS"]) for sample in telemetry])
    moving = np.asarray([
        abs(float(sample["velocity"][axis.name])) >= velocity_threshold
        for sample in telemetry
    ])
    gaps = np.diff(times)
    moving_indexes = np.flatnonzero(moving)
    reasons: list[str] = []
    if len(times) < 3 or len(gaps) < 2 or len(moving_indexes) < 2:
        reasons.append("insufficient telemetry samples")
        p95_gap = maximum_gap = float("inf")
        start_bracket = stop_bracket = float("inf")
    else:
        p95_gap = float(np.percentile(gaps, 95))
        maximum_gap = float(np.max(gaps))
        critical_gaps: list[float] = []
        interpolated_steady_gaps = 0
        for index, gap in enumerate(gaps):
            if gap <= TELEMETRY_MAX_GAP_S:
                continue
            before = telemetry[index]
            after = telemetry[index + 1]
            before_velocity = float(before["velocity"][axis.name])
            after_velocity = float(after["velocity"][axis.name])
            mean_velocity = (before_velocity + after_velocity) / 2.0
            before_position = before.get("position", {}).get(axis.name)
            after_position = after.get("position", {}).get(axis.name)
            observed_motion_gap = gap
            before_millis = before.get("millis")
            after_millis = after.get("millis")
            if before_millis is not None and after_millis is not None:
                board_gap = (
                    (int(after_millis) - int(before_millis)) & 0xFFFFFFFF
                ) / 1000.0
                if board_gap > 0.0:
                    observed_motion_gap = board_gap
            observed_velocity = (
                (float(after_position) - float(before_position))
                / observed_motion_gap
                if before_position is not None and after_position is not None
                else float("inf")
            )
            moving_interpolation_safe = (
                before.get("state") == "RUNNING"
                and after.get("state") == "RUNNING"
                and abs(before_velocity) >= velocity_threshold
                and before_velocity * after_velocity > 0.0
                and abs(after_velocity - before_velocity)
                <= max(velocity_threshold, abs(mean_velocity) * 0.05)
                and abs(observed_velocity - mean_velocity)
                <= max(velocity_threshold, abs(mean_velocity) * 0.10)
            )
            position_tolerance = (
                RHO_POSITION_TOLERANCE_MM if axis.name == "rho" else 0.001
            )
            stationary_interpolation_safe = (
                before.get("state") == "IDLE"
                and after.get("state") == "IDLE"
                and abs(before_velocity) < velocity_threshold
                and abs(after_velocity) < velocity_threshold
                and before_position is not None
                and after_position is not None
                and abs(float(after_position) - float(before_position))
                <= position_tolerance
            )
            if moving_interpolation_safe or stationary_interpolation_safe:
                interpolated_steady_gaps += 1
            else:
                critical_gaps.append(float(gap))
        maximum_critical_gap = max(critical_gaps, default=0.0)
        transitions = np.diff(moving.astype(np.int8))
        start_indexes = np.flatnonzero(transitions == 1) + 1
        stop_indexes = np.flatnonzero(transitions == -1)
        start_bracket = max(
            (float(times[index] - times[index - 1]) for index in start_indexes),
            default=float("inf"),
        )
        stop_bracket = max(
            (float(times[index + 1] - times[index]) for index in stop_indexes
             if index + 1 < len(times)),
            default=float("inf"),
        )
        if p95_gap > TELEMETRY_MAX_P95_GAP_S:
            reasons.append("telemetry p95 gap exceeds limit")
        if maximum_critical_gap > TELEMETRY_MAX_GAP_S:
            reasons.append("telemetry classification-critical gap exceeds limit")
        if max(start_bracket, stop_bracket) > TELEMETRY_MAX_TRANSITION_BRACKET_S:
            reasons.append("motion transition bracket exceeds limit")
    maximum_rtt = max(
        (float(sample.get("hostRequestRttS", 0.0)) for sample in telemetry),
        default=float("inf"),
    )
    return {
        "valid": not reasons,
        "sampleCount": len(telemetry),
        "p95GapS": round(p95_gap, 6),
        "maximumGapS": round(maximum_gap, 6),
        "maximumClassificationCriticalGapS": round(
            maximum_critical_gap if len(times) >= 3 else float("inf"), 6
        ),
        "interpolatedSteadyGapCount": (
            interpolated_steady_gaps if len(times) >= 3 else 0
        ),
        "motionStartBracketS": round(start_bracket, 6),
        "motionStopBracketS": round(stop_bracket, 6),
        "motionStartCount": int(len(start_indexes)) if len(times) >= 3 else 0,
        "motionStopCount": int(len(stop_indexes)) if len(times) >= 3 else 0,
        "maximumRequestRttS": round(maximum_rtt, 6),
        "invalidReasons": reasons,
    }


def save_timing_plot(
    path: Path,
    analysis: dict[str, np.ndarray],
    telemetry: list[dict[str, Any]],
    metrics: TimedAcousticMetrics,
    axis_config: AxisConfig = AXES["theta"],
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
    velocities = [float(sample["velocity"][axis_config.name]) for sample in telemetry]
    axes[1].plot(telemetry_times, velocities, color="#1874cd", linewidth=1.5)
    axes[1].axhline(0.0, color="black", linewidth=0.5)
    axes[1].set_ylabel(
        f"{axis_config.name.capitalize()} velocity\n({axis_config.velocity_unit})"
    )
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
    motion_flags = np.asarray([abs(velocity) >= 0.002 for velocity in velocities])
    motion_edges = np.diff(np.r_[False, motion_flags, False].astype(np.int8))
    motion_starts = np.flatnonzero(motion_edges == 1)
    motion_stops = np.flatnonzero(motion_edges == -1)
    for axis in axes:
        for start, stop in zip(motion_starts, motion_stops, strict=True):
            stop_index = min(int(stop), len(telemetry_times) - 1)
            axis.axvspan(
                telemetry_times[int(start)], telemetry_times[stop_index],
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
    expected_max_velocity: float,
) -> dict[str, Any]:
    axis = AXES[args.axis]
    rho_envelope_mm = (
        args.rho_excursion_mm
        if profile in ("screen", "gated")
        else RHO_TEST_MAX_EXCURSION_MM
    )
    board.recovering_stop()
    time.sleep(args.settle)
    recording_zero = time.monotonic()
    start_sample = _telemetry_sample(board, recording_zero, timeout_s=3.0)
    if not start_sample or start_sample.get("state") != "IDLE":
        raise RuntimeError(f"Board must be IDLE before a {axis.name} repeat")
    start_position = float(start_sample["position"][axis.name])
    other_start_position = float(start_sample["position"][axis.other_axis])
    audio_path = output_dir / f"{prefix}-{profile}-antlion-near.wav"
    plot_path = output_dir / f"{prefix}-{profile}-antlion-near-timing.png"
    timeline_path = output_dir / f"{prefix}-{profile}-timeline.json"
    output_dir.mkdir(parents=True, exist_ok=True)
    telemetry: list[dict[str, Any]] = []
    driver_samples: list[dict[str, Any]] = []
    gain_before = microphone_metadata(args.source)
    recorder = TimestampedRecorder(audio_path, args.source, args.rate)
    recorder.start()
    assert recorder.audio_start_monotonic is not None
    recorder_launch_offset = recorder.audio_start_monotonic - recording_zero
    command_offset = 0.0
    stop_request_offset: float | None = None
    next_poll = recording_zero

    def sample_drivers(phase: str) -> None:
        """Read UART only while stopped so it cannot blind motion telemetry."""
        for driver_role, dump_path in axis.driver_dump_paths:
            driver = board.get(dump_path)
            driver["driverRole"] = driver_role
            driver["samplePhase"] = phase
            driver["hostOffsetS"] = round(time.monotonic() - recording_zero, 6)
            driver_samples.append(driver)
            if not driver_is_healthy(driver):
                raise RuntimeError(
                    f"{driver_role} driver reported a fault at {phase}"
                )

    def collect_for(duration_s: float) -> None:
        nonlocal next_poll
        deadline = time.monotonic() + duration_s
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_poll:
                sample = _telemetry_sample(board, recording_zero)
                if sample:
                    telemetry.append(sample)
                    if axis.name == "rho":
                        rho_position = float(sample["position"]["rho"])
                        if rho_position < start_position - RHO_POSITION_TOLERANCE_MM:
                            raise RuntimeError(
                                f"Rho commanded inward of its start: {rho_position:.3f} < "
                                f"{start_position:.3f} mm"
                            )
                        if rho_position > (
                            start_position + rho_envelope_mm + RHO_POSITION_TOLERANCE_MM
                        ):
                            raise RuntimeError(
                                f"Rho exceeded its +{rho_envelope_mm:.0f} mm envelope"
                            )
                        if (
                            abs(float(sample["position"]["theta"]) - other_start_position)
                            > 0.001
                            or abs(float(sample["velocity"]["theta"])) > 0.002
                        ):
                            raise RuntimeError("Theta changed during a rho-only trial")
                # Schedule from completion, not the stale pre-request timestamp.
                next_poll = time.monotonic() + args.telemetry_interval
            time.sleep(0.005)

    def wait_for_motion_completion(motion_deadline: float) -> None:
        motion_seen = False
        consecutive_idle = 0
        evaluated_sample_count = len(telemetry)
        while time.monotonic() < motion_deadline and consecutive_idle < 3:
            collect_for(args.telemetry_interval)
            if len(telemetry) > evaluated_sample_count:
                evaluated_sample_count = len(telemetry)
                last = telemetry[-1]
                moving = (
                    abs(float(last["velocity"][axis.name]))
                    >= args.motion_velocity_threshold
                )
                if moving or last.get("state") in ("RUNNING", "STOPPING"):
                    motion_seen = True
                is_idle = motion_seen and last.get("state") == "IDLE" and not moving
                consecutive_idle = consecutive_idle + 1 if is_idle else 0
        if not motion_seen:
            raise RuntimeError(f"ESP32 telemetry did not confirm {axis.name} motion")
        if consecutive_idle < 3:
            raise TimeoutError

    try:
        sample_drivers("before-motion")
        collect_for(args.pre_idle)
        motion_deadline = time.monotonic() + args.duration
        try:
            if profile in ("screen", "gated"):
                if axis.name != "rho":
                    raise RuntimeError("The screen and gated profiles are rho-only")
                for target_mm in rho_segment_targets(
                    profile, args.rho_excursion_mm
                ):
                    board.post(
                        f"{axis.test_path_prefix}/segment",
                        {"targetMm": target_mm},
                    )
                    if command_offset == 0.0:
                        command_offset = time.monotonic() - recording_zero
                    wait_for_motion_completion(motion_deadline)
                    sample_drivers(f"after-segment-{target_mm:g}mm")
                    collect_for(args.gated_idle)
            else:
                board.post(f"{axis.test_path_prefix}/{profile}")
                command_offset = time.monotonic() - recording_zero
                wait_for_motion_completion(motion_deadline)
                sample_drivers("after-motion")
        except TimeoutError:
            board.stop()
            stop_request_offset = time.monotonic() - recording_zero
            raise RuntimeError(
                f"{axis.name.capitalize()} {profile} test did not complete within {args.duration:.1f}s; "
                "the partial run was stopped and cannot qualify a profile"
            )
        collect_for(args.post_idle)
        sample_drivers("after-post-idle")
    finally:
        try:
            board.recovering_stop()
        finally:
            recorder.stop()

    audio, captured_rate = read_wav(audio_path)
    gain_after = microphone_metadata(args.source)
    if captured_rate != args.rate or len(audio) < args.rate * (args.pre_idle + args.post_idle):
        raise RuntimeError("Antlion microphone recording is incomplete")
    moving_samples = [
        sample for sample in telemetry
        if abs(float(sample["velocity"][axis.name])) >= args.motion_velocity_threshold
    ]
    if len(moving_samples) < 5:
        raise RuntimeError(f"ESP32 telemetry did not confirm {axis.name} motion")

    final_position = float(telemetry[-1]["position"][axis.name])
    return_error = abs(final_position - start_position)
    return_tolerance = RHO_POSITION_TOLERANCE_MM if axis.name == "rho" else 0.001
    position_unit = "mm" if axis.name == "rho" else "rad"
    if return_error > return_tolerance:
        raise RuntimeError(
            f"{axis.name.capitalize()} {profile} finished {return_error:.4f} "
            f"{position_unit} from its starting position"
        )
    if axis.name == "rho":
        minimum_position = min(float(sample["position"]["rho"]) for sample in telemetry)
        maximum_position = max(float(sample["position"]["rho"]) for sample in telemetry)
        if minimum_position < start_position - RHO_POSITION_TOLERANCE_MM:
            raise RuntimeError(
                f"Rho {profile} commanded inward of its start: "
                f"{minimum_position:.3f} < {start_position:.3f} mm"
            )
        if maximum_position > start_position + rho_envelope_mm + RHO_POSITION_TOLERANCE_MM:
            raise RuntimeError(
                f"Rho {profile} exceeded its +{rho_envelope_mm:.0f} mm envelope"
            )
    motion_start = float(moving_samples[0]["hostOffsetS"])
    motion_end = float(moving_samples[-1]["hostOffsetS"])
    timing_quality = telemetry_timing_quality(
        telemetry, axis, args.motion_velocity_threshold,
    )
    timing_quality["audioEpochUncertaintyS"] = recorder.timestamp_uncertainty_s
    timing_quality["audioPrimingBlocksDiscarded"] = (
        recorder.priming_blocks_discarded
    )
    audio_duration_s = len(audio) / captured_rate
    host_audio_duration_s = (
        recorder.audio_end_monotonic - recorder.audio_start_monotonic
        if recorder.audio_end_monotonic is not None
        and recorder.audio_start_monotonic is not None
        else float("inf")
    )
    audio_duration_skew_s = abs(audio_duration_s - host_audio_duration_s)
    timing_quality["audioDurationS"] = round(audio_duration_s, 6)
    timing_quality["hostAudioDurationS"] = round(host_audio_duration_s, 6)
    timing_quality["audioDurationSkewS"] = round(audio_duration_skew_s, 6)
    if recorder.timestamp_uncertainty_s > 0.025:
        timing_quality["valid"] = False
        timing_quality["invalidReasons"].append(
            "audio sample-zero uncertainty exceeds limit"
        )
    if audio_duration_skew_s > AUDIO_MAX_DURATION_SKEW_S:
        timing_quality["valid"] = False
        timing_quality["invalidReasons"].append(
            "audio duration disagrees with host capture time"
        )
    recording_motion_start = motion_start - recorder_launch_offset
    recording_motion_end = motion_end - recorder_launch_offset
    metrics, analysis = analyze_timed(
        audio_path, recording_motion_start, recording_motion_end,
        minimum_gain_db=args.minimum_gain_db,
        minimum_persistence=args.minimum_persistence,
        telemetry=telemetry,
        recorder_launch_offset_s=recorder_launch_offset,
        axis=axis,
        velocity_threshold=args.motion_velocity_threshold,
        audio_epoch_uncertainty_s=recorder.timestamp_uncertainty_s,
        expected_max_velocity=(
            expected_max_velocity if profile in ("screen", "gated") else None
        ),
    )
    near_high_speed = high_speed_acoustic_level(
        audio_path, recorder_launch_offset, telemetry, axis,
        (
            None
            if args.reference_only or axis.name == "rho"
            else args.acceptable_ceiling_dbfs
        ),
        background_stable=metrics.background_stable,
        timing_valid=bool(timing_quality["valid"]),
        gain_stable=gain_before == gain_after,
        clipping_detected=metrics.clipping_detected,
    )
    recording_telemetry = [
        {**sample, "hostOffsetS": float(sample["hostOffsetS"]) - recorder_launch_offset}
        for sample in telemetry
    ]
    save_timing_plot(plot_path, analysis, recording_telemetry, metrics, axis)
    timeline = {
        "clock": "host monotonic seconds from capture orchestration start",
        "recorderLaunchOffsetS": round(recorder_launch_offset, 6),
        "recorderTimestampUncertaintyS": recorder.timestamp_uncertainty_s,
        "commandOffsetS": round(command_offset, 6),
        "stopRequestOffsetS": (
            round(stop_request_offset, 6) if stop_request_offset is not None else None
        ),
        "testCompletedNaturally": True,
        "axis": axis.name,
        "startPosition": round(start_position, 6),
        "finalPosition": round(final_position, 6),
        "returnError": round(return_error, 6),
        "rhoExcursionMm": rho_envelope_mm if axis.name == "rho" else None,
        "actualMotionStartS": round(motion_start, 6),
        "actualMotionEndS": round(motion_end, 6),
        "timingQuality": timing_quality,
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
        "timingQuality": timing_quality,
        "nearHighSpeed": near_high_speed,
        "testCompletedNaturally": True,
        "axis": axis.name,
        "startPosition": round(start_position, 6),
        "finalPosition": round(final_position, 6),
        "returnError": round(return_error, 6),
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
                (tone for tone in tones if tone.get("direction") == seed.get("direction")),
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
            "direction": seed.get("direction", "both"),
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


def repeat_confirmation_satisfied(
    profile_summaries: dict[str, dict[str, Any]], requested_repeats: int,
) -> bool:
    """Require multiple valid, motor-correlated qualification repeats."""
    return (
        requested_repeats >= 2
        and bool(profile_summaries)
        and all(
            not summary["provisionalScreen"]
            and summary["nearField"]["acceptedAsMotorNoise"]
            for summary in profile_summaries.values()
        )
    )


def interpolation_readback_confirmed(
    driver_samples: list[dict[str, Any]], expected_roles: set[str],
    expected_enabled: bool,
) -> bool:
    """Confirm every sampled driver reports the requested interpolation state."""
    sampled_roles = {sample.get("driverRole") for sample in driver_samples}
    return (
        bool(driver_samples)
        and sampled_roles == expected_roles
        and all(
            sample.get("settings", {}).get("chopconfReadValid", False)
            and bool(sample.get("settings", {}).get("interpolationTo256"))
            == expected_enabled
            for sample in driver_samples
        )
    )


def cmd_trial(args: argparse.Namespace) -> int:
    axis = AXES[args.axis]
    if not args.reference_only:
        if axis.name == "rho" and (
            args.acceptable_ceiling_dbfs is None or args.tone_ceiling_dbfs is None
        ):
            raise ValueError(
                "rho trials require rho-specific --acceptable-ceiling-dbfs and "
                "--tone-ceiling-dbfs, or --reference-only"
            )
        if args.acceptable_ceiling_dbfs is None:
            args.acceptable_ceiling_dbfs = DEFAULT_ACCEPTABLE_NEAR_HIGH_SPEED_CEILING_DBFS
        if args.tone_ceiling_dbfs is None:
            args.tone_ceiling_dbfs = DEFAULT_ACCEPTABLE_NEAR_TONE_CEILING_DBFS
    if args.repeats < 1 or args.repeats > 5:
        raise ValueError("--repeats must be between 1 and 5")
    if (args.duration <= 0 or args.pre_idle <= 0 or args.post_idle <= 0
            or args.gated_idle <= 0):
        raise ValueError(
            "--duration, --pre-idle, --post-idle, and --gated-idle must be positive"
        )
    if args.rated_current_ma <= 0:
        raise ValueError("--rated-current-ma must be positive")
    if not 1.0 <= args.rho_excursion_mm <= RHO_TEST_MAX_EXCURSION_MM:
        raise ValueError(
            f"--rho-excursion-mm must be between 1 and "
            f"{RHO_TEST_MAX_EXCURSION_MM:.0f}"
        )
    board = Board(args.board)
    board.recovering_stop()
    if axis.name == "rho":
        # Check the firmware boundary before changing speed or tuning values.
        preflight_rho_commissioning(board)
    board.post("/api/speed", {"speed": 10})
    tuning = apply_requested_settings(board, args)
    driver_settings = tuning[axis.driver_key]
    status = board.get("/api/status")
    if status.get("state") != "IDLE":
        raise RuntimeError(f"Board must be IDLE for a trial; state is {status.get('state')}")
    if axis.name == "rho":
        # Recheck after applying current/microstep settings. In commissioning,
        # a microstep change deliberately re-establishes logical zero.
        preflight_rho_commissioning(
            board, bool(driver_settings["interpolationEnabled"])
        )
        requested_profiles = (
            ("continuous", "stress") if args.profile == "both" else (args.profile,)
        )
        minimum_motion_s = max(
            rho_profile_distance_mm(profile, args.rho_excursion_mm)
            / float(tuning["motion"][axis.motion_velocity_key])
            for profile in requested_profiles
        )
        minimum_timeout_s = minimum_motion_s * 1.25 + 10.0
        gated_profiles = [
            profile for profile in requested_profiles
            if profile in ("screen", "gated")
        ]
        if gated_profiles:
            minimum_timeout_s += max(
                len(rho_segment_targets(profile, args.rho_excursion_mm))
                for profile in gated_profiles
            ) * args.gated_idle
        if args.duration < minimum_timeout_s:
            raise ValueError(
                f"--duration {args.duration:.1f}s is too short for the requested rho profile; "
                f"use at least {minimum_timeout_s:.0f}s (includes 25% ramp margin)"
            )
        preconditioning = precondition_rho_stealthchop(
            board, float(tuning["motion"][axis.motion_velocity_key]),
            driver_settings,
            float(tuning["limits"]["driverSenseResistorOhms"]),
        )
        if preconditioning["at2Validation"]["required"]:
            print(
                "[precondition] excluded +100 mm -> 0 StealthChop calibration motion",
                file=sys.stderr,
            )
        else:
            print(
                "[precondition] AT#2 motion not required for this driver mode",
                file=sys.stderr,
            )
    else:
        preconditioning = []

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
                float(tuning["motion"][axis.motion_velocity_key]),
            ))

    profile_summaries: dict[str, dict[str, Any]] = {}
    for profile in profiles:
        profile_repeats = [item for item in repeat_results if item["profile"] == profile]
        tones = confirmed_tones(profile_repeats)
        locked_levels = [
            item["metrics"]["timing_locked_a_weighted_dbfs"] for item in profile_repeats
            if item["metrics"]["timing_locked_a_weighted_dbfs"] is not None
        ]
        all_gain_stable = all(item["gainFingerprintStable"] for item in profile_repeats)
        all_timing_valid = all(
            item["timingQuality"]["valid"] for item in profile_repeats
        )
        required_pair_count = (
            len(rho_segment_targets(profile, args.rho_excursion_mm))
            if profile in ("screen", "gated") else 1
        )
        # Qualification is fail-closed on the loudest telemetry-confirmed
        # cruise gate in every repeat. The across-gate median remains a useful
        # diagnostic, but must never let quiet acceleration ramps or quieter
        # positions hide a loud sustained operating point.
        sustained_cruise_levels = [
            float(item["metrics"]["loudest_broadband_gate_excess_a_weighted_dbfs"])
            for item in profile_repeats
            if item["metrics"]["broadband_excess_a_weighted_dbfs"] is not None
            and item["metrics"]["loudest_broadband_gate_excess_a_weighted_dbfs"] is not None
            and item["metrics"]["background_stable"]
            and item["metrics"]["broadband_persistence"] >= args.minimum_persistence
            and item["metrics"]["on_off_pair_count"] == required_pair_count
            and item["metrics"]["on_off_consistency"] is not None
            and item["metrics"]["on_off_consistency"] >= 0.75
            and item["metrics"]["all_gates_sustain_commanded_velocity"] is True
            and not item["metrics"]["clipping_detected"]
            and item["timingQuality"]["valid"]
            and item["gainFingerprintStable"]
        ]
        valid_broadband = len(sustained_cruise_levels) == len(profile_repeats)
        all_background_stable = all(
            item["metrics"]["background_stable"] for item in profile_repeats
        )
        all_unclipped = all(
            not item["metrics"]["clipping_detected"] for item in profile_repeats
        )
        complete_pair_sets = all(
            item["metrics"]["on_off_pair_count"] == required_pair_count
            for item in profile_repeats
        )
        correlated_pair_sets = all(
            item["metrics"]["on_off_consistency"] is not None
            and item["metrics"]["on_off_consistency"] >= 0.75
            for item in profile_repeats
        )
        all_gates_sustain_velocity = all(
            item["metrics"]["all_gates_sustain_commanded_velocity"] is True
            for item in profile_repeats
        )
        valid_tones = (
            bool(tones) and all_gain_stable and all_timing_valid
            and all_background_stable and all_unclipped
            and complete_pair_sets and correlated_pair_sets
            and all_gates_sustain_velocity
        )
        valid_detection = valid_broadband or valid_tones
        level_valid = valid_broadband
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
            "provisionalScreen": profile == "screen",
            "qualificationEligible": profile != "screen",
            "nearField": {
                "microphone": "Antlion close to motor",
                "acceptedAsMotorNoise": valid_detection,
                "levelValidForComparison": level_valid,
                "broadbandMotorNoiseAccepted": valid_broadband,
                "timingLockedToneNoiseAccepted": valid_tones,
                "confirmedTimingLockedTones": tones,
                "acceptableTimingLockedToneCeilingDbfs": (
                    None if args.reference_only else args.tone_ceiling_dbfs
                ),
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
                    None if args.reference_only else (
                    bool(repeat_tone_levels)
                    and all(level <= args.tone_ceiling_dbfs for level in repeat_tone_levels)
                    )
                ),
                "loudestSustainedCruiseMotorExcessAWeightedDbfs": (
                    round(max(sustained_cruise_levels), 2)
                    if level_valid else None
                ),
                "motorNoiseMetric": (
                    "loudest telemetry-confirmed >=90% commanded-velocity gate, "
                    "minus louder adjacent-idle A-weighted power"
                ),
                "timingLockedToneBandAWeightedDbfs": (
                    round(float(np.median(locked_levels)), 2)
                    if valid_tones and locked_levels else None
                ),
                "allBackgroundWindowsStable": all(
                    item["metrics"]["background_stable"] for item in profile_repeats
                ),
                "gainFingerprintStable": all(
                    item["gainFingerprintStable"] for item in profile_repeats
                ),
                "allTelemetryTimingValid": all_timing_valid,
                "minimumOnOffPairCount": min(
                    int(item["metrics"]["on_off_pair_count"])
                    for item in profile_repeats
                ),
                "requiredOnOffPairCount": required_pair_count,
                "allGatesSustainCommandedVelocity": all_gates_sustain_velocity,
                "minimumOnOffConsistency": min(
                    float(item["metrics"]["on_off_consistency"])
                    for item in profile_repeats
                    if item["metrics"]["on_off_consistency"] is not None
                ) if any(
                    item["metrics"]["on_off_consistency"] is not None
                    for item in profile_repeats
                ) else None,
                "rawHighSpeedAWeightedDbfsDiagnostic": (
                    round(float(np.median(near_high_speed_levels)), 2)
                    if near_high_speed_levels else None
                ),
                "rawLoudestRepeatHighSpeedAWeightedDbfsDiagnostic": (
                    round(max(near_high_speed_levels), 2)
                    if near_high_speed_levels else None
                ),
                "rawHighSpeedMetricUsedForAcceptance": False,
                "acceptableMotorExcessCeilingAWeightedDbfs": (
                    None if args.reference_only else args.acceptable_ceiling_dbfs
                ),
                "everyRepeatWithinAcceptableReference": (
                    None if args.reference_only else (
                    valid_broadband
                    and all(
                        level <= args.acceptable_ceiling_dbfs
                        for level in sustained_cruise_levels
                    )
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
    sampled_driver_roles = {sample.get("driverRole") for sample in all_drivers}
    expected_driver_roles = {role for role, _ in axis.driver_dump_paths}
    driver_diagnostics = {
        driver_role: board.get(dump_path)
        for driver_role, dump_path in axis.driver_dump_paths
    }
    errors = board.get("/api/errors")
    if axis.name == "theta":
        steps_per_unit = (
            (200.0 * int(driver_settings["microsteps"]) / (2.0 * math.pi))
            * (60.0 / 16.0)
        )
    else:
        steps_per_unit = 50.0 * int(driver_settings["microsteps"])
    commanded_step_rate = (
        float(tuning["motion"][axis.motion_velocity_key]) * steps_per_unit
    )
    other_start = float(all_telemetry[0]["position"][axis.other_axis])
    other_position_tolerance = 0.05 if axis.other_axis == "rho" else 0.001
    other_velocity_tolerance = 0.002
    payload = {
        "kind": f"{axis.name}_timing_locked_trial",
        "axis": axis.name,
        "label": args.label,
        "recordedAt": stamp,
        "microphone": microphone_metadata(args.source),
        "settings": {"motion": tuning["motion"], axis.driver_key: driver_settings},
        "preconditioning": preconditioning,
        "summary": {
            "referenceOnly": args.reference_only,
            "repeatConfirmationSatisfied": repeat_confirmation_satisfied(
                profile_summaries, args.repeats
            ),
            "allTestsCompletedNaturally": all(
                item["testCompletedNaturally"] for item in repeat_results
            ),
            "byProfile": profile_summaries,
            "maxMeasuredVelocity": round(max(
                (abs(float(sample["velocity"][axis.name])) for sample in all_telemetry), default=0.0
            ), 6),
            "velocityUnit": axis.velocity_unit,
            "otherAxisStationary": all(
                abs(float(sample["position"][axis.other_axis]) - other_start)
                <= other_position_tolerance
                and abs(float(sample["velocity"][axis.other_axis]))
                < other_velocity_tolerance
                for sample in all_telemetry
            ),
            "allSequencesReturnedToStart": all(
                float(item["returnError"])
                <= (RHO_POSITION_TOLERANCE_MM if axis.name == "rho" else 0.001)
                for item in repeat_results
            ),
            "rhoNeverMovedInwardOfStart": (
                all(
                    min(float(sample["position"]["rho"]) for sample in item["telemetry"])
                    >= float(item["startPosition"]) - RHO_POSITION_TOLERANCE_MM
                    for item in repeat_results
                ) if axis.name == "rho" else None
            ),
            "plannerHealthy": all(
                int(sample["planner"]["underruns"]) == 0
                and int(sample["planner"]["maxConsecutiveUnderruns"]) == 0
                for sample in all_telemetry
            ),
            "commandedStepRateHz": round(commanded_step_rate, 1),
            "stepRateBudgetPercent": round(commanded_step_rate / 10000.0 * 100.0, 1),
            "allDriverUartResponsesValid": bool(all_drivers) and all(
                sample.get("uartResponseValid", False) for sample in all_drivers
            ) and sampled_driver_roles == expected_driver_roles,
            "requestedDriverInterpolationTo256": bool(
                driver_settings["interpolationEnabled"]
            ),
            "allDriverInterpolationReadbackConfirmed": (
                interpolation_readback_confirmed(
                    all_drivers, expected_driver_roles,
                    bool(driver_settings["interpolationEnabled"]),
                )
            ),
            "allDriversFaultFree": bool(all_drivers) and all(
                driver_is_healthy(sample) for sample in all_drivers
            ) and sampled_driver_roles == expected_driver_roles,
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
    expected_rho_currents = {8: 275, 10: 337, 12: 398, 13: 428, 14: 459}
    for expected_code, request_ma in expected_rho_currents.items():
        code, nominal_ma = driver_current_quantization(request_ma, True, 0.11)
        if code != expected_code or abs(nominal_ma - request_ma) > 1.0:
            raise RuntimeError(
                f"0.11 ohm current conversion failed for CS={expected_code}: "
                f"got CS={code}, {nominal_ma:.1f} mA"
            )

    valid_summary = {
        "gated": {
            "provisionalScreen": False,
            "nearField": {"acceptedAsMotorNoise": True},
        }
    }
    invalid_summary = {
        "gated": {
            "provisionalScreen": False,
            "nearField": {"acceptedAsMotorNoise": False},
        }
    }
    screen_summary = {
        "screen": {
            "provisionalScreen": True,
            "nearField": {"acceptedAsMotorNoise": True},
        }
    }
    if not repeat_confirmation_satisfied(valid_summary, 2):
        raise RuntimeError("Two valid qualification repeats were not confirmed")
    if (repeat_confirmation_satisfied(invalid_summary, 2)
            or repeat_confirmation_satisfied(valid_summary, 1)
            or repeat_confirmation_satisfied(screen_summary, 2)):
        raise RuntimeError("Invalid/provisional acoustic repeats were confirmed")

    interpolation_samples = [
        {
            "driverRole": role,
            "settings": {
                "chopconfReadValid": True,
                "interpolationTo256": enabled,
            },
        }
        for role, enabled in (("primary", False), ("companion", False))
    ]
    expected_roles = {"primary", "companion"}
    if not interpolation_readback_confirmed(
        interpolation_samples, expected_roles, False
    ):
        raise RuntimeError("Requested interpolation-off readback was rejected")
    if interpolation_readback_confirmed(
        interpolation_samples, expected_roles, True
    ):
        raise RuntimeError("Mismatched interpolation readback was accepted")

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
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(pcm.tobytes())
    metrics, analysis = analyze_timed(path, motion_start, motion_end)
    tones = [tone.frequency_hz for tone in metrics.timing_locked_tones]
    if not tones or min(abs(tone - 1250.0) for tone in tones) > 8.0:
        raise RuntimeError(f"Timed self-test did not recover the 1250 Hz motor tone: {tones}")
    if any(abs(tone - 700.0) <= 12.5 or abs(tone - 2100.0) <= 12.5 for tone in tones):
        raise RuntimeError(f"Timed self-test accepted an idle tone or transient: {tones}")

    # Adversarial gated case: changing random background, broadband-only motor
    # energy, and a tone that exists only while moving outbound. Four separate
    # on/off pairs must correlate with telemetry rather than one long time span.
    gated_duration = 12.0
    gated_samples = np.arange(round(rate * gated_duration)) / rate
    gated_rng = np.random.default_rng(7)
    # Vary both random and tonal room noise throughout the run, while ending
    # near the starting level so a simple pre/post check cannot solve the test.
    room_envelope = 1.0 + 0.55 * np.sin(2.0 * np.pi * gated_samples / 5.0)
    gated_background = gated_rng.normal(
        0.0, 0.0006 * room_envelope, len(gated_samples)
    )
    gated_background += (
        0.0008 * (1.0 + 0.35 * np.sin(2.0 * np.pi * gated_samples / 4.0))
        * np.sin(2.0 * np.pi * 900.0 * gated_samples)
    )
    gated_audio = gated_background.copy()
    intervals = ((2.0, 3.5, 0.1), (4.5, 6.0, -0.1),
                 (7.0, 8.5, 0.1), (9.5, 11.0, -0.1))
    gated_motion = np.zeros(len(gated_samples), dtype=bool)
    outbound = np.zeros(len(gated_samples), dtype=bool)
    for start, stop, velocity in intervals:
        mask = (gated_samples >= start) & (gated_samples < stop)
        cruise = (gated_samples >= start + 0.25) & (
            gated_samples < stop - 0.25
        )
        gated_motion |= mask
        outbound |= mask & (velocity > 0)
        # Long, quiet ramps must not dilute the louder sustained cruise metric.
        gated_audio[mask] += gated_rng.normal(0.0, 0.0008, int(np.sum(mask)))
        gated_audio[cruise] += gated_rng.normal(
            0.0, 0.004, int(np.sum(cruise))
        )
    gated_audio[outbound] += 0.008 * np.sin(
        2.0 * np.pi * 1375.0 * gated_samples[outbound]
    )
    # A loud, sustained sound in only one motion gate is unrelated and must
    # not become a persistent motor tone or a passing background-only result.
    unrelated_burst = (gated_samples >= 7.1) & (gated_samples < 7.8)
    gated_audio[unrelated_burst] += 0.006 * np.sin(
        2.0 * np.pi * 2400.0 * gated_samples[unrelated_burst]
    )
    gated_path = output_dir / "gated.wav"
    gated_pcm = np.clip(gated_audio * 32768.0, -32768, 32767).astype("<i2")
    with wave.open(str(gated_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(gated_pcm.tobytes())
    telemetry = []
    for sample_time in np.arange(0.0, gated_duration + 0.001, 0.05):
        velocity = 0.0
        for start, stop, candidate_velocity in intervals:
            if start <= sample_time < stop:
                edge_distance = min(sample_time - start, stop - sample_time)
                velocity = candidate_velocity * (
                    1.0 if edge_distance >= 0.25 else 0.5
                )
                break
        telemetry.append({
            "hostOffsetS": round(float(sample_time), 6),
            "hostRequestRttS": 0.004,
            "state": "RUNNING" if velocity else "IDLE",
            "velocity": {"theta": velocity, "rho": 0.0},
            "position": {"theta": 0.0, "rho": 0.0},
        })
    gated_metrics, _ = analyze_timed(
        gated_path, 2.0, 11.0, telemetry=telemetry, axis=AXES["theta"],
        velocity_threshold=0.002, expected_max_velocity=0.1,
    )
    gated_tones = gated_metrics.timing_locked_tones
    if not any(
        tone.direction == "outbound" and abs(tone.frequency_hz - 1375.0) <= 8.0
        for tone in gated_tones
    ):
        raise RuntimeError(f"Direction-specific tone was not recovered: {gated_tones}")
    if any(abs(tone.frequency_hz - 2400.0) <= 12.5 for tone in gated_tones):
        raise RuntimeError(f"Unrelated sustained burst was accepted: {gated_tones}")
    if (gated_metrics.on_off_pair_count != 4
            or gated_metrics.on_off_consistency < 0.75
            or gated_metrics.broadband_persistence < 0.65
            or not gated_metrics.all_gates_sustain_commanded_velocity):
        raise RuntimeError(f"Gated broadband correlation failed: {gated_metrics}")
    if (len(gated_metrics.broadband_gate_excess_a_weighted_dbfs) != 4
            or min(gated_metrics.gate_cruise_durations_s) < MINIMUM_GATE_CRUISE_S
            or gated_metrics.loudest_broadband_gate_excess_a_weighted_dbfs is None
            or gated_metrics.broadband_excess_a_weighted_dbfs is None
            or gated_metrics.broadband_excess_a_weighted_dbfs < -47.0):
        raise RuntimeError(
            "Quiet acceleration ramps diluted the sustained-cruise metric: "
            f"{gated_metrics}"
        )

    background_only = gated_background.copy()
    # Preserve the single unrelated burst while removing the synthetic motor.
    background_only[unrelated_burst] += 0.006 * np.sin(
        2.0 * np.pi * 2400.0 * gated_samples[unrelated_burst]
    )
    background_only_path = output_dir / "gated-background-only.wav"
    background_only_pcm = np.clip(
        background_only * 32768.0, -32768, 32767
    ).astype("<i2")
    with wave.open(str(background_only_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(background_only_pcm.tobytes())
    background_only_metrics, _ = analyze_timed(
        background_only_path, 2.0, 11.0, telemetry=telemetry,
        axis=AXES["theta"], velocity_threshold=0.002,
    )
    if (background_only_metrics.on_off_consistency is not None
            and background_only_metrics.on_off_consistency >= 0.75):
        raise RuntimeError(
            "Changing background without motor noise passed gated correlation"
        )
    quality = telemetry_timing_quality(telemetry, AXES["theta"], 0.002)
    if not quality["valid"] or quality["motionStartCount"] != 4:
        raise RuntimeError(f"Good synthetic telemetry failed timing validation: {quality}")
    telemetry_with_idle_gap = [
        sample for sample in telemetry
        if not 0.25 < float(sample["hostOffsetS"]) < 1.25
    ]
    idle_gap_quality = telemetry_timing_quality(
        telemetry_with_idle_gap, AXES["theta"], 0.002,
    )
    if not idle_gap_quality["valid"]:
        raise RuntimeError(
            "Known stationary telemetry gap failed timing validation: "
            f"{idle_gap_quality}"
        )
    telemetry_with_gap = [
        sample for sample in telemetry
        if not 5.5 < float(sample["hostOffsetS"]) < 6.5
    ]
    bad_quality = telemetry_timing_quality(
        telemetry_with_gap, AXES["theta"], 0.002,
    )
    if bad_quality["valid"]:
        raise RuntimeError("Telemetry timing validation accepted a one-second gap")

    drift_audio = audio.copy()
    drift_audio[samples > motion_end] *= 12.0
    drift_path = output_dir / "drifting-background.wav"
    drift_pcm = np.clip(drift_audio * 32768.0, -32768, 32767).astype("<i2")
    with wave.open(str(drift_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(drift_pcm.tobytes())
    drift_metrics, _ = analyze_timed(drift_path, motion_start, motion_end)
    if drift_metrics.background_stable:
        raise RuntimeError("Drifting-background self-test was incorrectly accepted")
    invalid_level = high_speed_acoustic_level(
        gated_path, 0.0, telemetry, AXES["theta"],
        background_stable=False,
    )
    if invalid_level.get("valid"):
        raise RuntimeError("High-speed level accepted an unstable background")

    clipped_audio = audio.copy()
    clipped_audio[0] = 1.0
    clipped_path = output_dir / "clipped.wav"
    clipped_pcm = np.clip(clipped_audio * 32768.0, -32768, 32767).astype("<i2")
    with wave.open(str(clipped_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(clipped_pcm.tobytes())
    clipped_metrics, _ = analyze_timed(clipped_path, motion_start, motion_end)
    if not clipped_metrics.clipping_detected:
        raise RuntimeError("Clipping self-test was not detected")
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
        "trial", help="run repeated telemetry-aligned idle -> axis motion -> idle recordings"
    )
    add_capture_options(trial)
    trial.set_defaults(duration=300.0)
    trial.add_argument("--axis", choices=sorted(AXES), default="theta")
    trial.add_argument("--label", required=True)
    trial.add_argument("--rated-current-ma", type=int, required=True)
    trial.add_argument(
        "--profile", choices=["continuous", "screen", "gated", "stress", "both"],
        default="both",
    )
    trial.add_argument(
        "--rho-excursion-mm", type=float, default=50.0,
        help=(
            "outward excursion for rho screen/gated segments; screens use four "
            "full out/back legs and qualification uses eight"
        ),
    )
    trial.add_argument("--repeats", type=int, default=3)
    trial.add_argument("--pre-idle", type=float, default=5.0)
    trial.add_argument("--post-idle", type=float, default=5.0)
    trial.add_argument(
        "--gated-idle", type=float, default=2.0,
        help="confirmed idle interval after every segment in the gated rho profile",
    )
    trial.add_argument("--stop-timeout", type=float, default=15.0)
    trial.add_argument("--telemetry-interval", type=float, default=0.05)
    trial.add_argument("--motion-velocity-threshold", type=float, default=0.002)
    trial.add_argument("--minimum-gain-db", type=float, default=6.0)
    trial.add_argument("--minimum-persistence", type=float, default=0.65)
    trial.add_argument(
        "--reference-only", action="store_true",
        help="record a new reference without applying acoustic pass/fail ceilings",
    )
    trial.add_argument(
        "--acceptable-ceiling-dbfs",
        type=float,
        help="maximum acceptable telemetry-selected Antlion level (theta default retained)",
    )
    trial.add_argument(
        "--tone-ceiling-dbfs",
        type=float,
        help="maximum acceptable timing-locked tone level (theta default retained)",
    )
    trial.add_argument("--run-current-ma", type=int)
    trial.add_argument("--hold-current-ma", type=int)
    trial.add_argument("--velocity", type=float, help="axis maximum velocity (rad/s for theta, mm/s for rho)")
    trial.add_argument("--accel", type=float, help="axis maximum acceleration (rad/s^2 or mm/s^2)")
    trial.add_argument("--jerk", type=float, help="axis maximum jerk (rad/s^3 or mm/s^3)")
    trial.add_argument("--microsteps", type=int, choices=[1, 2, 4, 8, 16, 32, 64, 128, 256])
    trial.add_argument("--mode", choices=["stealthchop", "spreadcycle"])
    trial.add_argument("--coolstep", choices=["on", "off"])
    trial.add_argument("--stealth-threshold", type=int)
    trial.add_argument("--current-scale", choices=["high", "standard"])
    trial.add_argument("--interpolation", choices=["on", "off"])
    trial.add_argument("--hold-delay", type=int, choices=range(16))
    trial.add_argument("--power-down-delay", type=int)
    trial.add_argument("--chopper-off-time", type=int, choices=range(1, 16))
    trial.add_argument("--hysteresis-start", type=int, choices=range(8))
    trial.add_argument("--hysteresis-end", type=int, choices=range(16))
    trial.add_argument("--blank-time", type=int, choices=range(4))
    trial.add_argument("--pwm-frequency", type=int, choices=range(4))
    trial.add_argument("--pwm-regulation", type=int, choices=range(1, 16))
    trial.add_argument("--pwm-limit", type=int, choices=range(16))
    trial.add_argument("--standstill-mode", type=int, choices=range(4))
    trial.add_argument("--automatic-gradient", choices=["on", "off"])
    trial.add_argument("--automatic-current", choices=["on", "off"])
    trial.add_argument("--pwm-offset", type=int, choices=range(256))
    trial.add_argument("--pwm-gradient", type=int, choices=range(256))
    trial.add_argument("--coolstep-lower", type=int, choices=range(1, 16))
    trial.add_argument("--coolstep-upper", type=int, choices=range(16))
    trial.add_argument("--coolstep-increment", type=int, choices=range(4))
    trial.add_argument("--coolstep-samples", type=int, choices=range(4))
    trial.add_argument("--coolstep-threshold", type=int)
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
