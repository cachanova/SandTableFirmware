#!/usr/bin/env python3
import argparse
import json
import random
import time
import urllib.parse
import urllib.request
from urllib.error import HTTPError, URLError


def get_json(url: str) -> dict:
    last_exc = None
    for attempt in range(5):
        try:
            with urllib.request.urlopen(url, timeout=10) as resp:
                return json.loads(resp.read().decode())
        except (HTTPError, URLError, OSError, json.JSONDecodeError) as exc:
            last_exc = exc
            time.sleep(2 + attempt)
    raise RuntimeError(f"Failed to fetch JSON from {url}: {last_exc}")


def post_form(url: str, data: dict) -> str:
    body = urllib.parse.urlencode(data).encode("utf-8")
    req = urllib.request.Request(url, data=body, method="POST")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    with urllib.request.urlopen(req, timeout=10) as resp:
        return resp.read().decode()


def log_event(log_path: str, payload: dict) -> None:
    payload["ts"] = time.time()
    line = json.dumps(payload, sort_keys=True)
    with open(log_path, "a", encoding="utf-8") as handle:
        handle.write(line + "\n")


def choose_patterns(base: str, count: int, seed: int | None) -> list[str]:
    if seed is not None:
        random.seed(seed)
    data = get_json(f"{base}/api/files")
    files = [item["name"] for item in data.get("files", []) if item.get("size", 0) > 0]
    if not files:
        raise RuntimeError("No non-empty pattern files returned by /api/files")
    if count >= len(files):
        random.shuffle(files)
        return files
    return random.sample(files, count)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run random patterns for long-duration testing.")
    parser.add_argument("--base", default="http://100.76.149.200", help="Base URL")
    parser.add_argument("--count", type=int, default=5, help="Number of patterns to run")
    parser.add_argument("--duration", type=int, default=1800, help="Seconds per pattern")
    parser.add_argument("--clearing", type=int, default=6, help="Clearing pattern (6=RANDOM)")
    parser.add_argument("--speeds", default="1,2,3,4,5,6,7,8,9,10",
                        help="Comma-separated speeds 1-10")
    parser.add_argument("--speed-interval", type=int, default=None,
                        help="Seconds between speed changes within a pattern "
                             "(default: evenly spread across duration)")
    parser.add_argument("--seed", type=int, default=None, help="Random seed")
    parser.add_argument("--log", default="tmp_capture/pattern_run.jsonl", help="Log output file")
    args = parser.parse_args()

    speeds = [int(s) for s in args.speeds.split(",") if s.strip()]
    if not speeds:
        raise RuntimeError("No speeds provided")

    patterns = choose_patterns(args.base, args.count, args.seed)

    print("Selected patterns:", ", ".join(patterns))
    log_event(args.log, {"event": "selected", "patterns": patterns, "speeds": speeds})

    try:
        post_form(f"{args.base}/api/pattern/stop", {})
    except (HTTPError, URLError, OSError) as exc:
        print(f"stop error {exc}")

    for idx, pattern in enumerate(patterns):
        speed_interval = args.speed_interval
        if speed_interval is None:
            speed_interval = max(1, args.duration // len(speeds))
        print(
            f"Starting {pattern} for {args.duration}s "
            f"(speed interval {speed_interval}s)"
        )
        log_event(args.log, {
            "event": "start",
            "pattern": pattern,
            "speeds": speeds,
            "speed_interval": speed_interval,
        })
        try:
            initial_speed = speeds[0]
            post_form(f"{args.base}/api/speed", {"speed": str(initial_speed)})
            post_form(f"{args.base}/api/pattern/start",
                      {"file": pattern, "clearing": str(args.clearing)})
        except (HTTPError, URLError, OSError) as exc:
            print(f"start error {exc}")
            log_event(args.log, {"event": "start_error", "pattern": pattern, "error": str(exc)})
            continue

        start_time = time.time()
        end_time = start_time + args.duration
        log_event(args.log, {
            "event": "speed_change",
            "pattern": pattern,
            "speed": initial_speed,
            "elapsed": 0.0,
        })
        next_change = start_time + speed_interval
        speed_idx = 1
        while True:
            now = time.time()
            if now >= end_time:
                break
            if now >= next_change:
                speed = speeds[speed_idx % len(speeds)]
                speed_idx += 1
                try:
                    post_form(f"{args.base}/api/speed", {"speed": str(speed)})
                    log_event(args.log, {
                        "event": "speed_change",
                        "pattern": pattern,
                        "speed": speed,
                        "elapsed": round(now - start_time, 2),
                    })
                except (HTTPError, URLError, OSError) as exc:
                    print(f"speed error {exc}")
                    log_event(args.log, {
                        "event": "speed_error",
                        "pattern": pattern,
                        "speed": speed,
                        "error": str(exc),
                        "elapsed": round(now - start_time, 2),
                    })
                next_change = now + speed_interval
            time.sleep(min(1, max(0, end_time - time.time())))

        try:
            post_form(f"{args.base}/api/pattern/stop", {})
        except (HTTPError, URLError, OSError) as exc:
            print(f"stop error {exc}")
            log_event(args.log, {"event": "stop_error", "pattern": pattern, "error": str(exc)})
        log_event(args.log, {"event": "stop", "pattern": pattern})
        print(f"Stopped {pattern}")

    log_event(args.log, {"event": "complete"})
    print("All patterns complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
