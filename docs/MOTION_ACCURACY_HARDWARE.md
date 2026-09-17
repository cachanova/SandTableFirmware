# Motion accuracy review and deployment

Follow-up (2026-09-17): the web/heap reserve fix and passing stationary, motion,
and cancellation checks are documented in [WEB_HEAP_RESERVE.md](WEB_HEAP_RESERVE.md).
The measurements below retain their original firmware and test context.

The motion branch was rebased onto main `a1f1750` before deployment, retaining
presence sensing, selected sound settings, allocation-failure handling, and
the interruptible file reader. The file-reader conflict was resolved by using
main's bounded/cancellable reader for both preflight and streaming. The old
independent-axis braking change is superseded by source-path braking.

## Findings corrected on hardware

1. **Working memory:** the first combined image used 106,800 bytes static RAM.
   Upload succeeded, but refreshing the library index remained deferred by the
   existing 24 KiB memory guard. No test motion had begun. Replacing redundant
   per-segment phase arrays with losslessly reconstructable double-precision
   profiles recovered 8,360 bytes. Caching derived path coefficients once,
   alongside the expanded current profile, recovered another 3,072 bytes.
   No task stack, queue capacity, coordinate precision, or admission threshold
   was reduced. Library upload/index/deletion then worked normally.
2. **Startup pulse scheduling:** on the first motor-connected run, preparing the
   initial queue left its first event timestamps in the past. Maximum reported
   lateness reached 66.8 ms on rho and 109.0 ms on theta despite zero underruns.
   The planner now shifts the complete prepared timeline immediately before
   arming the timer, with a 1 ms lead. Elapsed-time reads clamp that future
   start to zero. A regression injects 100 ms of startup delay and checks pulse
   timing, velocity, and exact final ledgers.
3. **Pattern-frame documentation:** the web playback controller already resets
   the logical theta origin at each new pattern. Documentation now distinguishes
   that frame reset from the planner's preservation of all unwrapped THR turns.

The final static RAM is **95,368 bytes**, versus **97,864 bytes** for main's
presence image. The larger coordinate queue adds 2,048 runtime bytes, leaving
combined fixed storage **448 bytes below main**. Flash is **1,465,941 bytes**
(93.2% of the OTA application slot). A native segment is 312 bytes and the
planner is 15,960 bytes. Reconstructing compact profiles reproduces phase times,
positions, velocities, accelerations, and evaluated motion bit-for-bit in the
focused regression cases.

## Verification

Installed source commit **`325758c`** through the production OTA environment.
The flashed image SHA-256 is
`ba9c83974ec344746eed92c15b3b5e2bb7485fa6194d19bb48193a85f0e454af`.
Warm reboot and automatic homing succeeded. Both fitted axes answered; the
unused rho companion stayed disabled. Bench mode was false.

Eight functional checks passed across **703 requests** on the final image:
whole-file rejection before any motion; mixed-path completion at exact rounded
motor targets; pause/stable hold; resume and speed change; a 130-point streamed
path under image/SD and web reads; zero-tolerance corner mode; a full turn
emitting exactly **12,000 theta pulses**; and no reboot or unexpected errors.

The complete example circle at 212.5 mm radius then passed with two live SSE
clients and repeated PNG, library, diagnostic and SD reads. It completed in
**84.31 seconds including cleanup**, yielded **2,264 SSE events** and 245
telemetry samples, respected the sampled 30 mm/s ball-speed ceiling, and ended
at rho 212.5000 mm / theta 6.280044 rad (the exact rounded source target).

Across these motor runs, final counters recorded **zero planner underruns** and
**zero axis pulse-timing outliers** above the 100 microsecond threshold.
Maximum software pulse lateness was **94 microseconds theta / 98 microseconds
rho**. Callback telemetry separately recorded four gaps over threshold, with
a maximum of 109 microseconds. The table was left IDLE at the circle endpoint,
with speed 5, brightness 50, original driver/homing settings, original library
and playlist, and the new motion defaults. The deliberately generated invalid
file error was captured and cleared; temporary patterns were removed.

All synthetic and 24-pattern native tests pass. The dedicated 50 µs accuracy
suite passes normally and under AddressSanitizer/UndefinedBehaviorSanitizer.
The final 26-file audits at 5/10 and 10/10 preserve the reported geometry
improvement and have zero final step discrepancies or simulated underruns.
Production serial/OTA and theta/rho commissioning OTA environments build.
The 104 Python tests, seven browser suites, and the native pattern-reader
sanitizer check pass. The audit artifacts record the tested source hashes.

A host-harness correction was needed for the circle load test: SSE requests
must send `Accept: text/event-stream`. The earlier circle completed with correct
ledgers and ordinary web/SD traffic, but its two stream requests received 404;
that attempt is not counted as a successful SSE load test.

## Heavy web-load reserve remains unqualified

The final image was subjected to the existing GET-only presence load harness,
requesting 180 seconds of HTML, library and PNG reads with two SSE clients.
It stopped when the conservative lifetime minimum byte-heap reading fell below
the unchanged 8 KiB guard. This is a **failed reserve check**, not a full
180-second stress pass.

- The failing sample was at 127.29 seconds; total run/recovery was 133.97 seconds.
  The minimum reading was **7,072 bytes**.
- There were 520 requests, 96 retryable HTTP 503s, and 123 events on each SSE
  connection. Four image identities were checked for byte consistency.
- Sampled current byte heap stayed at or above 16,116 bytes; sampled largest
  blocks stayed at or above 9,716 bytes. Current byte heap recovered from
  38,248 to 37,872 bytes, with a 32,756-byte largest block after cleanup.
- No reboot or new device error occurred; presence reception recovered.

The SDK minimum aggregates per-region minima that can occur at different times;
it is not proof that simultaneous free heap reached exactly 7,072 bytes. It is
still treated conservatively by the test. After the subsequent motor/circle load checks, the cumulative conservative
minimum was lower still, at **2,616 bytes**; current byte heap at the final
readout was 34,844 bytes with a 24,564-byte largest block. The successful motion
and SSE checks do not turn this into a heap-reserve pass. Main already carries this unresolved
reserve qualification (its documented run stopped at 5,764 bytes); the motion
release does not claim to resolve it. The memory guard and stress threshold
remain intact. See [the prior presence qualification](PRESENCE_HARDWARE_TEST.md).

## Limits and reproduction

Positions and step counts here are the commanded/executed software ledger.
Timing telemetry samples software immediately before the GPIO edge. These
checks do not independently measure electrical pulses, missed mechanical steps,
backlash, or ball tracking through sand. No independent camera/encoder
measurement or operator observation was available for this deployment.

Saved driver/motion/homing settings, the original library and playlist, speed,
and selected brightness are compared before/after testing. Temporary patterns
are removed. Reboot resets the presence detector's volatile calibration, so
its preserved feature/settings do not imply a calibrated room measurement.

Host artifacts (images, scripts, request/telemetry traces, and failed attempts)
are retained under `/tmp/sisyphus-motion-deploy/`. The compact durable result
is [hardware-validation.json](motion-accuracy/hardware-validation.json).
Run the host checks from the [implementation report](MOTION_ACCURACY_IMPLEMENTATION.md).
The stationary load command is:

```sh
python scripts/presence_stress.py --duration 180 --presence
```

That command requires the table to be idle and issues no motion commands.
Motor-connected checks must use the normal homing-enabled production image;
the bench image with an assumed origin is unsuitable for connected motors.
