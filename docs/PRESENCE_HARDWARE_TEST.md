# Presence hardware test attempt — 2026-09-16

## State of verification

Main was pulled with `--ff-only` (already current at `eeb368d`) and
`feat/presence-sensing` rebased onto it. The queued-motion conflict was resolved
by preserving main's allocation-failure handler alongside presence automation.
Commit `5f0118b` adds response ownership on the two presence GET handlers and
separate byte-addressable heap telemetry, plus a reproducible GET-only load test.

The production presence image was uploaded successfully over OTA to
`100.76.149.200`. The uploader received `Result: OK` / `Success` at 22:14:56
US/Central. **The device did not return to HTTP/ARP afterward. Presence hardware
stress testing has therefore not passed or even started.** A physical
power-cycle or USB serial connection was requested. No calibration, deliberate
motion command, upload of SD files, or motion-setting change was issued during
this attempt. Production boot automatically homes; its actual post-install
homing result could not be observed.

An unreachable device cannot be rolled back over OTA. The previous main image
is available at `/tmp/sisyphus-final-review/review.bin`, with SHA-256
`9b165aa5319443a1cb8a89e7d3ff1b6482c7915eb83d275a0e528423be054778`, matching
the production deployment recorded in FINAL_REVIEW. Do not infer the startup
failure's cause without serial/boot evidence; successful upload does not prove
successful boot, nor does loss of connectivity alone establish a CSI defect.

## Host and build checks

- All eleven ESP32 environments built successfully after the final telemetry
  edit. Production uses 104,256 B static RAM and 1,446,629 B application flash,
  leaving 126,235 B in the application slot.
- The archived deployed-main build reports 103,096 B static RAM and 1,418,589 B
  flash: the updated branch adds 1,160 B static RAM and 28,040 B flash. Runtime
  allocations, including the ping stack and network buffers, are additional.
- Native synthetic tests and all 24 repository pattern simulations passed.
- All seven browser test files and all 102 Python tests passed.
- ASan/UBSan presence stress passed ten runs: ten million synthetic frames,
  10,000 calibrations, and 60 million callback rate-gate attempts. The tested
  sensing loop performed no C++ heap allocations.
- ASan/UBSan response-buffer, transport, JSON persistence, pattern-reader, and
  actual playlist tests passed. Persistence checks used the documented
  unoptimized host flags; an initial optimized `-Werror` compile encountered an
  ArduinoJson `maybe-uninitialized` warning, not a runtime sanitizer failure.

## Installed-main baseline

Before flashing, the stationary production firmware completed a 90-second
concurrent GET load (about 96 seconds including cleanup): existing full-size
image downloads, page/API polling, and two SSE clients. There were no transport
failures, device resets, or device errors. All repeated image downloads matched
their earlier SHA-256 values; the two streams received 87 events each.

There were 48 HTTP 503 busy responses; bounded retries recovered. Median status
latency was 51.68 ms, p95 149.51 ms, maximum 291.07 ms. Median full-image request
time was 1.53 seconds, p95 2.75 seconds, maximum 3.82 seconds. Free internal heap
went from 75,500 B to 75,492 B; the minimum-ever internal heap ended at 40,768 B.
These are short-run observations, not a leak proof.

The initial harness attempts stopped early because they did not retry monitor
503s and omitted the SSE `Accept: text/event-stream` header. Those harness issues
were corrected before the completed baseline; the stopped attempts are not
counted as successful load tests or as firmware regressions.

### Heap measurements are not interchangeable

The pinned Arduino SDK implements `ESP.getFreeHeap`, `getMinFreeHeap`, and
`getMaxAllocHeap` with `MALLOC_CAP_INTERNAL`. That includes 32-bit-only regions;
the reported 38,900-byte largest block is not necessarily usable for ordinary
network byte buffers. The presence guard uses `MALLOC_CAP_8BIT` already.

`/api/system/info` now adds `heap8Bit`, `largestFree8BitBlock`, and
`minimumFree8BitHeap` using that same capability. Existing fields are unchanged.
The load harness uses the new values for its guards when present. The old-main
baseline cannot provide directly equivalent API-level byte-pool measurements;
its logged `Largest` field does use `MALLOC_CAP_8BIT`.

## Reproduction and pending work

```sh
# GET only; requires IDLE and existing images, never starts a pattern.
python scripts/presence_stress.py --duration 120 > baseline.json
# After authorized install, successful boot and fresh CSI reception:
python scripts/presence_stress.py --duration 180 --presence > presence.json
```

The harness limits client count and duration, verifies exact response lengths,
checks repeated image hashes, tracks SSE events and new logged errors, and stops
on motion/state changes, reboot, transport failure, repeated monitor refusal,
or low-memory guards. It does not force OOM, reconnect the AP, mutate the library,
calibrate the detector, or certify that the physical room is empty.

Once connectivity is recovered, verify boot logs and unchanged tuning/library,
live CSI reception, calibration completion, the lighting action, byte-addressable
heap and stack margins under concurrent load, and recovery after client cleanup.
Actual empty-room accuracy requires operator coordination. AP reconnect behavior,
long-duration leaks, and concurrent motor timing remain separate qualifications.

Artifacts: `/tmp/sisyphus-presence-install.ok5QPm/` contains the exact uploaded
BIN/ELF, OTA log, and pre-install tuning/status/library/playlist snapshots.
Image SHA-256:
`23859f5cfd26f828d9a5f55a0b9fe4bf6a0f381eb8e403ea30ba4e61b4eebefc`.
The completed main baseline is `/tmp/presence-main-baseline.json`; host/build
logs are `/tmp/presence-rebase-*.log`. These temporary artifacts are local and
are not durable replacements for this checked-in report.
