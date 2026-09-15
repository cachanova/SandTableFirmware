#!/usr/bin/env python3
"""Read-only capture of a production homing cycle, optionally across OTA reboot.

Does not start, stop, confirm, or command motion. A firmware success still
needs independent physical review during qualification.
"""
import argparse
import json
import time
from pathlib import Path

import requests
from acoustic_tuner import Board, DEFAULT_BOARD, utc_stamp
from rho_homing_tuner import HomingTraceCollector


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", default=DEFAULT_BOARD)
    parser.add_argument("--wait-for-reboot", action="store_true")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--output-dir", default="tuning-recordings/rho-startup")
    args = parser.parse_args()
    board = Board(args.board)
    initial = board.get("/api/status")
    highest_uptime = initial["uptime"]
    reboot_seen = not args.wait_for_reboot
    collector = HomingTraceCollector()
    statuses = []
    errors = []
    capture_error = None
    deadline = time.monotonic() + args.timeout
    terminal = None
    while time.monotonic() < deadline:
        try:
            status = board.get("/api/status", timeout_s=0.5)
            reboot_seen |= status["uptime"] < highest_uptime
            highest_uptime = max(highest_uptime, status["uptime"])
            if reboot_seen and status["homing"]["cycle"] > 0:
                if args.wait_for_reboot or status["homing"]["cycle"] > initial["homing"]["cycle"]:
                    statuses.append(status)
                    if capture_error is None:
                        try:
                            collector.add(board.get("/api/tuning/homing/trace", timeout_s=0.5))
                        except RuntimeError as error:
                            capture_error = str(error)
                    if status["state"] != "HOMING":
                        terminal = status
                        break
        except requests.RequestException as error:
            errors.append(str(error))
        time.sleep(0.1)
    result = {
        "kind": "read_only_startup_observation", "recordedAt": utc_stamp(),
        "initial": initial, "waitForReboot": args.wait_for_reboot,
        "rebootSeen": reboot_seen, "terminal": terminal,
        "traceComplete": terminal is not None and capture_error is None,
        "captureError": capture_error, "connectionErrors": errors,
        "statuses": statuses, "trace": collector.result(),
        "physicalReview": "required; not performed by this script",
    }
    output = Path(args.output_dir)
    output.mkdir(parents=True, exist_ok=True)
    path = output / (result["recordedAt"] + "-startup-observation.json")
    path.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"result": str(path), "terminal": terminal,
                      "traceComplete": result["traceComplete"], "captureError": capture_error}))
    return 0 if terminal and terminal["state"] == "IDLE" and result["traceComplete"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
