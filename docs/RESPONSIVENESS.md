# SD, playback, and web responsiveness investigation

Device: `100.76.149.200`. Work is based on `bca8e0c`, the merged
`codex/rho-final-sound` deployment. The adopted driver settings, motion limits,
and homing procedure are unchanged.

## Confirmed causes and changes

- The old library scan performed repeated Arduino `exists`, `open`, and metadata
  calls in the web logic task. A 29-pattern scan took 5,860 ms; a rescan after
  upload also produced a task-watchdog reset. A separate worker now enumerates
  names and reads metadata without opening each pattern or image. The previous
  index remains available while rebuilding; revisions refresh the browser.
- Starting a file slept and fed the planner while holding the motion mutex.
  A pending STOP could also publish `fileLoading=false` before a new LOAD was
  processed. Generation acknowledgement now gates initial planner feeding;
  the motor task waits for the active reader even when initial waypoints have
  zero distance. File and upload paths accommodate the allowed 80-character
  filenames in nested directories.
- An end-of-file log ran on every motor tick while the final segments drained.
  It filled the 64-entry log ring and delayed calls needing the motion mutex.
  Removed the repeated message.
- Browser startup waited for sequential, unbounded requests before polling
  status. Interval polls could accumulate, and failed starts left optimistic
  “Starting” state. Status now starts independently, shares one in-flight
  request, has a body-inclusive deadline, and reports disconnected/uncertain
  command results. Library-loading responses retry without discarding a
  previously loaded list. Empty pattern files are rejected explicitly.
- Pattern rows have visible, lazily loaded previews and no size labels.
  Thumbnail requests are serialized, restricted to visible rows, and bounded
  by a timeout. White-on-transparent PNGs have a dark backing. Overlay requests
  handle status arriving before the library and discard stale image results.
- The canvas batches received positions into animation frames, caches the ball
  sprite, repaints only its previous area, and throttles coordinate text. It
  retains all foreground path samples, bounds hidden-tab queues, and breaks
  traces across reconnect gaps. The guide is darkened for contrast against
  sand. Clearing the trace preserves the last stationary ball. A local Chromium
  render using the device's cached Spiral7 PNG and 90 actual THR waypoints
  verified guide, trace, and ball together. Every Spiral7 waypoint falls within
  two pixels of its PNG guide with the existing 380-pixel radius and positive
  screen Y mapping. This is a local rendering check, not live motion validation.
- Position events now sample position, velocity, and completion under one
  motion lock. They include stopped velocity and a one-second stationary
  heartbeat, without building long queues of stale positions on slow clients.
- A retained ESP32 panic backtrace proved that Arduino `cbuf::resize`, called
  by `AsyncResponseStream::write`, threw on allocation during concurrent image,
  log, and start requests. Assembled responses now use bounded 1 KB blocks and
  return a complete 503 document when allocation is refused. The panic capture
  uses RTC memory and the pinned ESP-IDF panic API; retain the matching ELF
  when symbolizing addresses.
- PNG reads in the network callback measured about 5.95 seconds of a 6.3-second
  59,534-byte transfer. Reducing the SPI request from 40 MHz (driver-clamped to
  25 MHz) to 10 MHz did not help and was reverted. The latest implementation
  reads images on a background task through a bounded 4 KB stream, yielding
  between chunks. Cancellation releases the response immediately and lets the
  worker close its file safely. This last change still needs live validation.
- Wi-Fi modem sleep is disabled for this mains-powered controller. One measured
  full-page transfer improved from about 4 seconds to 0.88 seconds; SD image
  throughput did not improve. This is a single observation, not a benchmark.

## Measurements and verification

| Check | Original | Changed firmware |
| --- | ---: | ---: |
| Library metadata scan | 5,860 ms / 29 files | 828–1,228 ms / 30 files |
| Short-pattern telemetry median | 94 ms | 29 ms |
| Short-pattern telemetry maximum | 175 ms | 108 ms |
| Add all patterns to playlist | not measured | 25 ms |
| Completed pattern to first coordinate of next | not measured | 84 ms |

The extra file is the temporary `codex_response_probe.thr` fixture. Its four
coordinates use theta zero and rho fractions `0`, `0`, `0.00470588`, and
`0.00235294` (approximately 0 → 2 → 1 mm). Playback and repeated starts passed
without images, with no reported queue underruns. Original library metadata
and saved tuning matched after the first updated deployment.

Local checks passed:

```sh
pio run -e esp32dev_ota
./run_all_tests.sh
node test/test_responsiveness_ui.cjs
node test/test_pattern_previews_ui.cjs
node test/test_canvas_trace_ui.cjs
node test/test_homing_abort_ui.cjs
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I lib/WebServer/src test/test_response_buffer.cpp -o /tmp/test_response_buffer
/tmp/test_response_buffer
```

The motion suite includes synthetic cases and all 24 repository pattern files.
The native buffer checks cover fragmented output, allocation failure, size
limits, and cancellation cleanup. UI checks cover stalled response bodies,
shared polling, disconnected recovery, indexing retries, independent startup,
duplicate start prevention, preview loading, overlay races, canvas coordinate
mapping, frame batching, path corners, stationary updates, and reconnect gaps.

## Live verification still required

The combined image/log/repeated-start test on the bounded-response build lost
connectivity after two start acknowledgements. The device stopped answering
HTTP and ARP and did not recover during observation. That result does **not**
establish that all concurrent-load failures are fixed. A physical reset was
requested; the background-image-reader build has not yet been deployed.

After recovery:

1. Retrieve `/api/status` and `/api/logs?limit=64` before another deployment.
   Preserve reset reason and any retained panic frames with the matching ELF.
2. Deploy the current OTA build, allow normal homing to complete, and test
   image transfers alongside status, logs, and repeated short-pattern starts.
   Check uptime, heap recovery, returned image bytes, and motion errors.
3. Validate upload/rescan without a reset, a long filename, and empty-file
   rejection. Test preview cancellation and browser behavior on the device.
4. Remove `codex_response_probe.thr` and any further test-only files. Restore
   the original empty playlist with loop disabled and clearing enabled.

Session measurements and host scripts are under
`/tmp/sisyphus-responsiveness/`; they are diagnostic scratch files, not durable
test infrastructure. `panic.elf` there matches the earlier symbolized cbuf OOM,
not the later connectivity loss.
