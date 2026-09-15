#!/usr/bin/env python3
"""Measure contact scatter from marked RHO homing traces; never move hardware."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def contact_scatter(artifact: dict[str, Any], axis: int = 1) -> dict[str, Any]:
    settings = artifact["settings"]
    if not settings.get("traceContactMarkers"):
        return {"usable": False, "reason": "trace predates explicit contact markers"}
    scale = float(settings["homingStepsPerMm"])
    start = float(settings["knownRhoStartMm" if axis == 1 else "knownCompanionStartMm"])
    contacts = [sample for sample in artifact["trace"]
                if int(sample["a"]) == axis and sample.get("b", 0) > 0]
    if not contacts:
        return {"usable": False, "reason": "no detected contact markers"}
    coordinates: list[float] = []
    relative_errors: list[float] = []
    passes: list[int] = []
    coordinate = 0.0
    for sample in contacts:
        pass_number = int(sample["n"])
        if pass_number != len(passes) + 1:
            return {"usable": False, "reason": "contact markers are missing or duplicated"}
        if settings.get("rollingSearch"):
            next_coordinate = (int(sample["o"]) + int(sample["s"])) / scale - start - float(settings["runwayMm"])
            error = next_coordinate - coordinate
            coordinate = next_coordinate
        else:
            expected = start + float(settings["runwayMm"]) if pass_number == 1 else float(settings["backoffMm"])
            error = int(sample["s"]) / scale - expected
            coordinate += error
        coordinates.append(coordinate)
        relative_errors.append(error)
        passes.append(pass_number)
    spans = [max(coordinates[i:i+3]) - min(coordinates[i:i+3])
             for i in range(max(0, len(coordinates) - 2))]
    return {
        "usable": True,
        "passes": passes,
        "contactCoordinatesMm": [round(value, 4) for value in coordinates],
        "returnErrorsMm": [round(value, 4) for value in relative_errors],
        "threeContactSpansMm": [round(value, 4) for value in spans],
        "minimumThreeContactSpanMm": round(min(spans), 4) if spans else None,
        "firmwareState": artifact["terminalStatus"]["state"],
        "instrumentedPass": artifact["instrumentedPass"],
        # A final camera check does not label every earlier contact as home.
        "physicalContactLabels": "requires synchronized external observation",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="+", type=Path)
    parser.add_argument("--tolerances", type=float, nargs="+",
                        default=[0.2, 0.3, 0.4, 0.5, 0.6, 0.8])
    args = parser.parse_args()
    for path in args.artifacts:
        result = contact_scatter(json.loads(path.read_text()))
        spans = result.get("threeContactSpansMm", [])
        result["firstAgreementPassAtTolerance"] = {
            str(tolerance): next((i + 3 for i, span in enumerate(spans)
                                  if span <= tolerance), None)
            for tolerance in args.tolerances
        }
        print(json.dumps({"artifact": str(path), **result}))


if __name__ == "__main__":
    main()
