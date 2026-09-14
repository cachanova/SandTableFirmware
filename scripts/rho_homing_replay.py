#!/usr/bin/env python3
"""Replay candidate StallGuard settings against captured main-RHO traces."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


BASELINE_SAMPLES = 48
CONFIRMED_COLLAPSE_RATIO = 0.70
CLUSTERED_COLLAPSE_RATIO = 0.58
HARD_COLLAPSE_RATIO = 0.35
DEEP_PULSE_RATIO = 0.10
# The trace timestamp is recorded after the first UART read, while firmware
# starts its detector clock before that read. Allow one read's latency when
# replaying the time gate; the independent emitted-step gate stays exact.
FIRST_READ_LATENCY_MS = 3


def replay_phase(
    samples: list[dict[str, Any]], *, ratio: float, votes: int,
    minimum_steps: int, steps_per_second: int,
    external_steps_per_full_step: int = 8,
) -> dict[str, Any] | None:
    """Mirror StallGuardDetector with the firmware's fresh-full-step gate."""
    if not samples or votes < 1 or steps_per_second < 1:
        return None
    decision_size = 2 * votes - 1
    history: list[int] = []
    last_sample_steps: int | None = None
    first_steps = int(samples[0]["s"])
    first_time_ms = int(samples[0]["t"]) - FIRST_READ_LATENCY_MS
    ignore_ms = math.ceil(
        max(0, minimum_steps - first_steps) * 1000 / steps_per_second
    )

    for sample in samples:
        if not sample["v"]:
            continue
        steps = int(sample["s"])
        if (last_sample_steps is not None and
                steps - last_sample_steps < external_steps_per_full_step):
            continue
        last_sample_steps = steps
        history.append(int(sample["g"]))
        history = history[-(BASELINE_SAMPLES + decision_size):]
        if len(history) < BASELINE_SAMPLES + decision_size:
            continue

        baseline_values = sorted(history[-(BASELINE_SAMPLES + decision_size):-decision_size])
        baseline = (
            baseline_values[BASELINE_SAMPLES // 2 - 1]
            + baseline_values[BASELINE_SAMPLES // 2]
        ) / 2
        if (int(sample["t"]) - first_time_ms < ignore_ms
                or steps < minimum_steps or baseline < 4):
            continue

        decision = history[-decision_size:]
        soft_threshold = baseline * ratio
        confirmed_threshold = baseline * min(ratio, CONFIRMED_COLLAPSE_RATIO)
        soft_count = sum(value <= soft_threshold for value in decision)
        confirmed_count = sum(value <= confirmed_threshold for value in decision)
        consecutive = maximum_consecutive = 0
        for value in decision:
            consecutive = consecutive + 1 if value <= soft_threshold else 0
            maximum_consecutive = max(maximum_consecutive, consecutive)
        clustered = (
            maximum_consecutive >= 3
            and min(decision) <= baseline * CLUSTERED_COLLAPSE_RATIO
        )
        deep = (
            confirmed_count >= votes
            and min(decision) <= baseline * HARD_COLLAPSE_RATIO
        )
        repeated_deep_pulses = (
            sum(value <= baseline * DEEP_PULSE_RATIO for value in decision) >= 2
            and int(sample["g"]) <= baseline * DEEP_PULSE_RATIO
        )
        if repeated_deep_pulses or (
            int(sample["g"]) <= soft_threshold
            and soft_count >= votes
            and (clustered or deep)
        ):
            return {"steps": steps, "sg": int(sample["g"]),
                    "baseline": baseline}
    return None


def replay_artifact(
    artifact: dict[str, Any], ratio: float, votes: int,
    minimum_travel_ms: int | None = None,
    coarse_minimum_mm: float | None = None,
) -> dict[str, Any]:
    settings = artifact["settings"]
    if coarse_minimum_mm is None:
        coarse_minimum_mm = float(settings.get("coarseMinimumTravelMm", 3.0))
    steps_per_mm = int(settings["homingStepsPerMm"])
    rate = round(settings["coarseVelocityMmS"] * steps_per_mm)
    if minimum_travel_ms is None:
        minimum_travel_ms = int(settings["minimumTravelMs"])
    reports = {}
    for phase, minimum_steps, required_votes, label in (
        (2, math.ceil(coarse_minimum_mm * steps_per_mm),
         max(5, votes // 3), "coarse"),
        (4, math.ceil(rate * minimum_travel_ms / 1000), votes, "precision"),
    ):
        selected = [sample for sample in artifact["trace"]
                    if int(sample["a"]) == 1 and int(sample["p"]) == phase]
        reports[label] = replay_phase(
            selected, ratio=ratio, votes=required_votes,
            minimum_steps=minimum_steps, steps_per_second=rate,
            external_steps_per_full_step=int(settings["homingMicrosteps"]),
        )
    return reports


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="+", type=Path)
    parser.add_argument("--percent", type=int, nargs="+",
                        default=[55, 60, 65, 70, 75, 80, 85])
    parser.add_argument("--votes", type=int, nargs="+", default=[5, 9, 13])
    parser.add_argument("--minimum-travel-ms", type=int,
                        help="override precision minimum travel for replay")
    parser.add_argument("--coarse-minimum-mm", type=float,
                        help="override coarse detector arming distance")
    args = parser.parse_args()
    if args.coarse_minimum_mm is not None and args.coarse_minimum_mm < 0:
        parser.error("--coarse-minimum-mm must be nonnegative")
    for path in args.artifacts:
        artifact = json.loads(path.read_text(encoding="utf-8"))
        print(path)
        for percent in args.percent:
            for votes in args.votes:
                result = replay_artifact(
                    artifact, percent / 100, votes, args.minimum_travel_ms,
                    args.coarse_minimum_mm)
                print(json.dumps({"percent": percent, "votes": votes,
                                  "coarse": result["coarse"],
                                  "precision": result["precision"]}))


if __name__ == "__main__":
    main()
