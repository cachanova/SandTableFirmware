#!/usr/bin/env python3
"""Replay THR files with production defaults and save reproducible geometry metrics.

Usage: python scripts/audit_motion_accuracy.py [--samples 32] [--speed 1]
No device connection, upload, or firmware change. Requires g++; plotting is
optional and uses matplotlib if available. Run from any directory.
"""
import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("patterns", nargs="*", type=Path)
    parser.add_argument("--samples", type=int, default=32)
    parser.add_argument("--speed", type=float, default=1)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/motion-accuracy/after")
    args = parser.parse_args()
    paths = args.patterns or sorted((ROOT / "test/test_motion/patterns").glob("*.thr")) + sorted((ROOT / "example_patterns").glob("*.thr"))
    args.output.mkdir(parents=True, exist_ok=True)
    source_hashes = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                     for path in sorted((ROOT / "lib/PolarControl/src").glob("*"))
                     if path.suffix in (".cpp", ".hpp")}
    with tempfile.TemporaryDirectory(prefix="sisyphus-accuracy-") as temp:
        binary = Path(temp) / "audit"
        subprocess.run(["g++", "-O2", "-std=c++17", "-DNATIVE_BUILD",
                        "-I", str(ROOT / "lib/PolarControl/src"), "-I", str(ROOT / "test/test_motion"),
                        str(ROOT / "test/test_motion/accuracy_audit.cpp"), "-o", str(binary)], check=True)
        checks = subprocess.check_output([str(binary), "--self-test"], text=True)
        (args.output / "numerical-checks.json").write_text(checks)

        def run(path):
            path = path.resolve()
            result = subprocess.run([str(binary), str(path), str(args.samples), str(args.speed)],
                                    text=True, capture_output=True, timeout=1800)
            if result.returncode:
                raise RuntimeError(f"{path.name}: {result.stderr.strip()}")
            data = json.loads(result.stdout)
            data["file"] = str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)
            data["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
            print(f'{path.name}: shape {data["profile_shape_mm"]["max"]:.4f} mm; '
                  f'nominal mechanism {data["nominal_mechanism_mm"]["max"]:.4f} mm', flush=True)
            return data

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(run, paths))

    report = {
        "baseline_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "working_tree_modified": bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, text=True).strip()),
        "source_sha256": source_hashes,
        "configuration": {"rho_steps_per_mm": 400, "theta_steps_per_rad": 12000 / (2 * 3.141592653589793),
                          "nominal_theta_steps_per_revolution": 12000, "radius_mm": 425,
                          "rho_v_a_j": [5.5, 20, 100], "theta_v_a_j": [0.225, 2, 10],
                          "ball_max_velocity": 30, "ball_max_acceleration": 100, "corner_tolerance_mm": 0.10,
                          "speed": args.speed, "native_timer_us": 250, "feed_interval_us": 10000},
        "method": "Real streaming planner with native timer. First-point approach excluded. Uniform time samples of each committed profile; nearest distance to its own polar segment. Maxima are sampled, not certified bounds. Shared-progress comparison is a geometric reference, not a replacement planner.",
        "results": results,
    }
    (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    with (args.output / "summary.csv").open("w") as f:
        writer = csv.writer(f)
        writer.writerow(["file", "points", "segments", "shape_max_mm", "shape_p95_segment_max_mm",
                         "nominal_mechanism_max_mm", "endpoint_max_mm", "common_progress_max_mm",
                         "planned_seconds", "both_axes_stop", "underruns"])
        for d in results:
            writer.writerow([d["file"], d["points"], d["segments"], d["profile_shape_mm"]["max"],
                             d["profile_shape_mm"]["p95_segment_max"], d["nominal_mechanism_mm"]["max"],
                             d["max_endpoint_mm"], d["common_progress_exact_scale_mm"]["max"],
                             d["planned_seconds"], d["both_axes_stop"], d["underruns"]])
    try:
        import matplotlib
        matplotlib.use("Agg")
        matplotlib.rcParams["svg.hashsalt"] = "sisyphus-motion-accuracy"
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib unavailable; JSON/CSV are complete")
        return
    ranked = sorted(results, key=lambda d: d["profile_shape_mm"]["max"], reverse=True)
    fig, ax = plt.subplots(figsize=(10, 9), layout="constrained")
    labels = [Path(d["file"]).name for d in ranked]
    ax.barh(labels, [d["profile_shape_mm"]["max"] for d in ranked], color="#ad4934", label="Current profile shape error")
    ax.scatter([d["common_progress_exact_scale_mm"]["max"] for d in ranked], labels, color="#176a70", s=22, label="Common progress + exact scale reference")
    ax.invert_yaxis()
    ax.set(xlabel="Sampled maximum distance to intended polar segment (mm)", title="THR path accuracy · 425 mm radius · production defaults")
    ax.legend(loc="lower right")
    fig.savefig(args.output / "corpus-error.svg", metadata={"Date": None})
    plt.close(fig)
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.5), layout="constrained")
    selected = ranked[:2] + [next((d for d in results if d["file"].endswith("example_patterns/spiral.thr")), ranked[-1])]
    for ax, d in zip(axes, selected):
        trace = d["worst_trace"]
        ax.plot([p[0] for p in trace], [p[1] for p in trace], color="#176a70", label="Intended polar segment")
        ax.plot([p[2] for p in trace], [p[3] for p in trace], color="#ad4934", linestyle="--", label="Current planned path")
        ax.set_aspect("equal", adjustable="datalim")
        ax.set(title=f'{Path(d["file"]).name}\n{d["profile_shape_mm"]["max"]:.3f} mm max deviation', xlabel="x (mm)", ylabel="y (mm)")
        ax.grid(alpha=.2)
    axes[0].legend(fontsize=8)
    fig.savefig(args.output / "path-comparison.svg", metadata={"Date": None})
    plt.close(fig)

    for path in args.output.glob("*.svg"):
        path.write_text("\n".join(line.rstrip() for line in path.read_text().splitlines()) + "\n")


if __name__ == "__main__":
    main()
