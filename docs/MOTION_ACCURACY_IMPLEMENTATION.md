# THR motion accuracy implementation

Implemented on `assess/motion-accuracy`, starting from `main` at `f39a7ea`.
The [assessment](MOTION_ACCURACY_ASSESSMENT.md) and its original artifacts remain
as the historical baseline. The release was rebased onto `main` at `a1f1750`,
preserving the memory fixes and presence sensing. See
[deployment and hardware checks](MOTION_ACCURACY_HARDWARE.md) for the installed
image, review follow-ups, measurements, and remaining physical-validation limits.

## Result

The planner now uses one scalar S-curve to traverse a shared polar path. Both
axes therefore follow the same geometric progress. Exact nominal angular
conversion, double-precision coordinates, and nearest absolute motor-step
rounding remove cumulative scale and large-angle float errors.

A configurable 0.10 mm continuous-path tolerance permits smooth corners.
The implementation uses quintic curves passing through the source waypoints;
their Bezier controls stay inside a bounded corridor around each original
polar segment and within its coordinate ranges. Shared tangents and zero
geometric second derivatives at joins permit continuous velocity and acceleration.
Setting the tolerance to zero restores linear polar interpolation and stops at
incompatible junctions. Motor-step quantization is additional to this tolerance.

The corpus replay compares the running planner with the source THR geometry,
including streaming, finite lookahead, and final executed step counts. It does
not measure physical ball tracking. The initial approach is excluded from
these fidelity metrics.

All **26 files, 158,112 coordinates, and 158,025 nonduplicate segments** were
replayed at each speed. Final theta/rho ledgers match their exact rounded
source targets in every file; both runs report **zero simulated underruns**.

| Speed | Baseline profile deviation | New continuous-path deviation | New nominal mechanism deviation, including quantization |
|---|---:|---:|---:|
| 5/10 (default) | 2.311 mm | 0.0552 mm | 0.1518 mm |
| 10/10 | 4.197 mm | 0.0552 mm | 0.1512 mm |

These are sampled maxima across the corpus, with 33 time samples per committed
segment. The baseline profile metric isolates its independent-axis distortion
using its own rounded endpoints; it omits the much larger cumulative gear-scale
error. The new nominal-mechanism metric uses exact nominal gearing and includes
nearest-step quantization. It remains a model, not an observed physical result.
For example, the sparse example spiral improves from 1.523 mm of baseline
profile deviation at full speed to 0.0063 mm continuous / 0.0391 mm quantized.

At full speed, the 24 test patterns take **1.58–2.26×** their previous planned
duration. The example spiral takes 1.02× and the circle 1.60×. Slower playback is
an explicit tradeoff of the new ball limit and curvature-aware path following.

- [Full-speed JSON](motion-accuracy/after/results.json) and [CSV](motion-accuracy/after/summary.csv)
- [Default-speed JSON](motion-accuracy/after-speed-5/results.json) and [CSV](motion-accuracy/after-speed-5/summary.csv)
- [Full-speed corpus plot](motion-accuracy/after/corpus-error.svg)
- [Representative path comparisons](motion-accuracy/after/path-comparison.svg)


## Changes

- **Nominal gearing:** theta uses `12000 / (2*pi)` steps/radian at the default
  microstep setting, rather than truncating to 1909. Tests cover positive and
  negative complete turns, up to 1000 turns, and large unwrapped angles.
- **Shared motion:** a single scalar profile drives theta and rho. Lookahead
  reconciles scalar boundary speeds and keeps profiles already committed to
  the event queue immutable. Infeasible profiles cannot silently become
  discontinuous rest-to-rest moves.
- **Motion limits:** geometric derivative bounds project motor velocity,
  acceleration, and jerk limits onto the scalar profile. Cartesian limits
  include centripetal and Coriolis acceleration. Exact Bezier subdivision
  tightens conservative bounds; unchanged geometry reuses its computed limits.
- **Long moves and endpoints:** integer 64-bit microsecond offsets replace
  repeated float time additions. Queued event timestamps retain their existing
  32-bit near-future representation. Generation completes only after the final
  absolute motor target has been queued, including when it falls on a horizon.
  Initial queued timestamps are anchored immediately before arming the timer,
  so ESP32 queue-preparation time cannot cause an overdue first-pulse burst.
- **Pause, stop, speed changes:** queued pulses remain immutable. Braking follows
  the current curve, continuing across existing source segments if necessary
  until a feasible zero-acceleration braking point. Resume stores the unfinished
  curve and scalar distance as well as the remaining targets. This can delay a
  normal stop relative to independent-axis braking; emergency shutdown remains
  a separate immediate-stop path.
- **File validation:** the streaming task preflights the complete file before
  publishing coordinates. A shared parser rejects malformed/nonfinite data,
  radius outside [0,1], missing separators, extra tokens, embedded NULs, lines
  over 127 bytes, and signed motor-step range overflow. Empty and unreadable
  files fail loading. It reports a source line and checks for cancellation
  during preflight. Existing blank lines, comma separators, and comments work.
  The unused permissive `FilePosGen` was removed. Main's interruptible 4 KiB
  reader and allocation-failure handling are preserved during preflight.
- **Settings and UI:** ball speed, ball acceleration, and smoothing tolerance
  are exposed through the tuning API/UI and persisted with existing settings.
  Older settings files use defaults for the new fields. Axis tuning values
  remain intact. Microstep scale changes retain the existing origin-reset /
  rehoming behavior; driver tuning requires an idle machine.
- **Tests and tooling:** the root test runner delegates to the maintained
  script. Native builds use optimization to keep multi-hour simulated moves
  practical. A separate native environment runs accuracy regressions at the
  production 50 microsecond period. Firmware now explicitly uses its configured
  C++17 standard instead of allowing the platform's C++11 flag to override it.

## Controls and tradeoffs

| Setting | Default | Accepted range | Meaning |
|---|---:|---:|---|
| Ball Max Speed | 30 mm/s | 0.1–200 mm/s | Planar speed ceiling; multiplied by the speed slider |
| Ball Max Acceleration | 100 mm/s² | 0.1–2000 mm/s² | Bound on planar acceleration |
| Corner Smoothing | 0.10 mm | 0–0.25 mm | Continuous geometric deviation allowance; zero follows the exact polar segments |

These are software defaults, not a measured ball/magnet tracking envelope.
The tighter ball-speed limit and corner handling increase playback duration.
The axis limits, ball limits, curvature, and remaining stopping distance can
all reduce speed below the slider's requested fraction. Bounds are conservative;
a future optimization could recover some speed while maintaining them.

An absolute move to the first coordinate remains visible as an approach line.
The existing playback controller resets logical theta to zero at each new
pattern (including playlist transitions), so the pattern frame starts at the
current physical angle. Within that frame, the planner preserves absolute THR
angles and all unwrapped turns; it does not choose shortest angular moves.

The final production build uses **95,368 bytes static RAM (29.1%)** and
**1,465,941 bytes flash (93.2%)**, including current main's presence sensing.
Static RAM is 2,496 bytes below that main baseline; the 256-entry coordinate
queue grows by 2,048 bytes at runtime. Combined fixed storage is therefore
448 bytes smaller. Task stacks and queue capacities are unchanged.

Buffered segments store independent double-precision S-curve values and path
geometry. A shared evaluation cache reconstructs their derived phase state and
polynomial coefficients. Tests require bit-identical reconstructed motion,
and the native segment/planner sizes are now 312 / 15,960 bytes. The initial
680-byte segment version blocked index refresh on hardware and was replaced.
The hardware report covers the resulting throughput and heap measurements;
those tests do not establish electrical pulse quality or physical ball error.

## Verification and reproduction

```sh
bash scripts/run_all_tests.sh
pio run -e esp32dev
python scripts/audit_motion_accuracy.py --jobs 3
python scripts/audit_motion_accuracy.py --speed .5 --jobs 3 \
  --output docs/motion-accuracy/after-speed-5
python test/test_motion_timing_api.py
python test/test_rho_adopted_defaults.py
node test/test_ui_recovery.cjs
node test/test_responsiveness_ui.cjs
node test/test_canvas_trace_ui.cjs
```

All commands above passed, including all 24 legacy corpus patterns and both
26-file audits. The accuracy regressions test exact gearing and rounding, parser/preflight
validation, exact and smoothed geometry, independently evaluated axis and
planar derivatives, varied segment lengths and reversals, large angles,
1024/8192-second generation offsets, clock rollover, endpoint horizon flushing,
source-curve pause/resume and speed changes, completed-tail handling, and
streaming ring refills. The 24-pattern legacy suite additionally exercises
completion, boundary velocity continuity, and underruns. Its older mechanical
configuration remains a secondary compatibility test; the audit uses production
settings. Corpus replay callbacks run at 250 microseconds, while targeted
accuracy regressions run at 50 microseconds. Deterministic native feeding cannot
establish ESP32 timing margins or electrical pulse quality.

Artifact JSON includes configuration, source hashes captured before compilation,
pattern hashes, and the base commit. Error maxima are sampled rather than a
proof of the worst point on each replayed curve. The geometric corridor itself
is bounded by construction; step quantization and physical error are separate.

## Physical validation before adopting new limits

1. Verify the actual microstep settings, gearing, radial origin, and travel.
   At a fixed radius, measure closure after 1, 10, and 100 complete turns in
   both directions. Compare against the nominal 12,000 theta steps/revolution.
2. Measure repeated radial targets from both directions at several radii to
   distinguish zero/scale error from backlash. Rehome and repeat to assess
   origin repeatability.
3. With an independent camera or positional reference, compare the magnet/ball
   path for a circle, sparse spiral, reversals, and mixed theta/rho corners.
   Start slowly and compare exact mode with the 0.10 mm smoothing mode.
4. Pause/resume and change speed while accelerating, cruising, and nearing a
   waypoint. Check for extra lines and repeated path portions, allowing for the
   deliberately preserved first-point approach.
5. Under concurrent SD reads, web/SSE traffic, and pattern loading, record queue
   underruns, timing telemetry, task stack high-water marks, free heap, and
   physical ball lag. Choose speed/acceleration limits from those measurements.

The web trace and motor-step telemetry share the commanded position ledger.
They are useful diagnostics, but an independent measurement is needed to claim
physical movement accuracy or calibrate the remaining mechanical error.
