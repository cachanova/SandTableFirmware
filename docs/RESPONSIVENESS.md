# SD, playback, and web responsiveness investigation

Follow-up (2026-09-17): the web/heap reserve fix and passing stationary, motion,
and cancellation checks are documented in [WEB_HEAP_RESERVE.md](WEB_HEAP_RESERVE.md).
The measurements below retain their original firmware and test context.

Device: `100.76.149.200`. Work is based on `bca8e0c`, the merged
`codex/rho-final-sound` deployment. The adopted driver settings, motion limits,
and homing procedure are unchanged. The final branch is rebased onto
`a4d91e4`, including the separate Abort homing button availability fix.

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
  filenames in nested directories. A one-slot overwrite mailbox now preserves
  the latest LOAD/STOP request when rapid starts outpace SD opening.
- An end-of-file log ran on every motor tick while the final segments drained.
  It filled the 64-entry log ring and delayed calls needing the motion mutex.
  Removed the repeated message.
- Browser startup waited for sequential, unbounded requests before polling
  status. Interval polls could accumulate, and failed starts left optimistic
  “Starting” state. Status now starts independently, shares one in-flight
  request, has a body-inclusive deadline, and reports disconnected/uncertain
  command results. Library-loading responses retry without discarding a
  previously loaded list. Empty pattern files are rejected explicitly. An explicit
  status 503 displays TABLE BUSY and recovers on the next poll; it does not claim
  the device disconnected.
- Pattern rows have visible, lazily loaded previews and no size labels.
  Thumbnail requests are serialized, restricted to visible rows, and bounded
  by a timeout. Their background matches the sand canvas, and transparent white
  lines are darkened. Both upload pages generate 128-pixel PNG derivatives;
  existing images fall back to the original until a derivative is available.
  Active overlays take priority, cancelling and requeuing previews. Duplicate
  overlay requests are suppressed; stale results cannot replace a newer image.
- The canvas batches received positions into animation frames, caches the ball
  sprite, repaints only its previous area, and throttles coordinate text. It
  retains received path samples even when animation frames pause, rasterizes
  hidden-tab batches to bound memory, and breaks
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
  return a complete 503 document when allocation is refused. Status, errors,
  and upload acknowledgements use the same bounded response implementation;
  status and upload acknowledgements may use a reserved control budget. This
  also eliminates occasional empty 200 status responses under upload load. The panic capture
  uses RTC memory and the pinned ESP-IDF panic API; retain the matching ELF
  when symbolizing addresses.
- PNG reads in the network callback measured about 5.95 seconds of a 6.3-second
  59,534-byte transfer. Reducing the SPI request from 40 MHz (driver-clamped to
  20 MHz) to 10 MHz did not help and was reverted. The latest implementation
  reads images on a background task through a bounded 4 KB stream, yielding
  between chunks. Cancellation releases the response immediately and lets the
  worker close its file safely. Sector-sized stdio buffers reduced the same
  image's measured SD time to about 280–420 ms. A bounded five-millisecond
  wait for prefetched bytes avoids repeated TCP poll delays when the stream
  is briefly empty, without doing SD I/O in the network callback. Final full
  image transfers took 0.84–2.11 seconds for 60–179 KB and matched the originals
  byte for byte in the earlier isolated run. ESPAsyncWebServer 3.9.5 consumes
  in-flight credits even on `RESPONSE_TRY_AGAIN`, which can stall background
  streams. Project-owned known-length responses now handle this directly; the
  SDK retains its default configuration for other response types.
- USB serial captured a second allocation panic in the pattern-index vector
  during uploads and rescans: 47 KB total free memory but only a 1.7 KB largest
  block. The cache and sorted index now use segmented deques, preserve the
  previous index on allocation failure, and retry under memory pressure.
  Index scans and image streams use mutual admission to avoid rebuilding two
  indexes alongside an active image response. Upload activity defers indexing
  until writes have settled for 750 ms, coalescing batches of uploads.
- USB diagnostics isolated the offline failure: HTTP and ARP stopped while
  tasks remained alive, Wi-Fi remained associated, and heap integrity passed.
  Temporary transmit instrumentation captured ESP_ERR_NO_MEM with about 50 KB
  total free heap but only 1.1–1.5 KB contiguous blocks. Reconnecting Wi-Fi
  restored service without rebooting. Disabling sleep alone did not fix this.
  `WifiMemory.cpp` now wraps the public `esp_wifi_init` boundary and reserves
  six static TX buffers, with six static RX, twelve dynamic RX, and two cached
  TX buffers. This avoids allocating DMA transmit packets in the fragmented
  runtime heap, with a smaller budget than Arduino's broad static-buffer
  toggle. The other initialization fields are preserved. This configuration
  is tied to the pinned Arduino/IDF version; revisit it on platform upgrades.
  See the [IDF Wi-Fi buffer documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-guides/wifi.html#wi-fi-buffer-configure).
- AsyncTCP callbacks are pinned to core 0 with CONFIG_ASYNC_TCP_RUNNING_CORE=0;
  the running chip confirmed this in its first HTTP callback. Motion and homing
  remain on core 1. Pinning WebTask alone did not constrain AsyncTCP. Shared
  memory and SD contention still require the limits described above. Measured
  AsyncTCP stack use stayed below 3.1 KB during load; an 8 KB stack retains
  about 5 KB headroom and returns 8 KB to the shared heap.
- Wi-Fi modem sleep is disabled for this mains-powered controller. In paired
  image/status runs, status median decreased from 68.5 to 27 ms and maximum
  from 356 to 123 ms. This is a latency improvement separate from the TX-memory
  fix. Disconnect events and periodic association/RSSI diagnostics remain;
  temporary transmit probes and serial diagnostic commands were removed.

## Measurements and verification

| Check | Original | Changed firmware |
| --- | ---: | ---: |
| Library metadata scan | 5,860 ms / 29 files | 1,258–1,599 ms / 30 files including thumbnail metadata |
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
node test/test_thumbnail_upload_ui.cjs
node test/test_ui_recovery.cjs
node test/test_ui_gzip.cjs
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I lib/WebServer/src test/test_response_buffer.cpp -o /tmp/test_response_buffer
/tmp/test_response_buffer
# UIPagesGz.h is generated; PlatformIO builds it, a bare g++ needs it first.
python3 scripts/build_ui_gz.py
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Itest/support/known_length_response -Ilib/WebServer/src \
  test/test_known_length_response.cpp -o /tmp/test_known_length_response
/tmp/test_known_length_response
```

The motion suite includes synthetic cases and all 24 repository pattern files.
The native buffer checks cover fragmented output, allocation failure, size
limits, and cancellation cleanup. UI checks cover stalled response bodies,
shared polling, disconnected recovery, indexing retries, independent startup,
duplicate start prevention, preview loading, overlay races, canvas coordinate
mapping, frame batching, path corners, stationary updates, and reconnect gaps.

## Latest USB-only device validation

The final core-0 build completed six byte-identical full-image downloads and
25 concurrent status requests with no errors, busy responses, or uptime reset.
Images took 0.84–2.11 seconds; status median was 54 ms and maximum 230 ms.
Saved motor tuning matched the pre-test snapshot.

A heavier run uploaded all 27 generated thumbnails while downloading six full
images and polling status. All uploads succeeded without retries. Of 41 status
requests, 40 returned valid status and one returned an explicit 503. There were
no observed resets or network outages. The first image took 14.3 seconds during
the bulk upload; later images took 0.89–2.48 seconds. Successful status requests
had a 194.5 ms median and 1.95-second maximum during this deliberately heavy
load. Bulk writes can still slow reads; bounded busy responses are intentional.
The 27 derivatives total 84,851 bytes versus 2,553,542 bytes of source PNGs.

Read-only Chromium checks passed all 13 assertions: page load 377 ms, 30 rows
visible at 569 ms, first preview at 1.08 seconds, selection 0.8 ms, and status
median 27 ms / maximum 115 ms. Scrolling fetched additional previews, thumbnail
backgrounds matched the canvas (rgb 238,234,222), and size labels were absent.
Stationary SSE positions and the center ball rendered correctly, with maximum
render time 0.2 ms. The successful run had no JavaScript or API errors. An
initial navigation failed to initialize within 20 seconds. The later expanded
suite traced this to attaching Chromium to its Cast extension background target
instead of the page; choosing the page target fixes the harness.

## Expanded no-motor operation tests

At the operator's request, tests were extended with motors disconnected. The
normal firmware passed 125 API checks over 190 requests (41.3 ms median,
769 ms maximum). These covered all four pages, diagnostics, rejected unhomed
motion, input validation, speed and lighting, tuning preservation, playlist
editing/save/load, uploads, 80-character filenames, original/thumbnail image
replacement, cache validation, empty files, and recursive fixture deletion.
The first attempt was interrupted by a POWERON_RESET when serial capture was
reopened; no panic was recorded. The repeat with serial left open had no reset.
The unavailable RHO-service endpoint correctly returns 404 in production, and
homing with the missing driver returns a failure instead of pretending success.

A temporary, isolated bench worktree then assumed a logical origin and enabled
both planner axes while retaining truthful driver-availability reporting. It
applied rho 20 mm/s, 100 mm/s², 1,000 mm/s³ and theta 3 rad/s, 10 rad/s²,
100 rad/s³ in RAM only, with speed 10. No settings were saved at these limits.
This exercises actual SD loading, planner execution, timer counters and SSE on
the ESP32, but does not verify physical motor motion or sensorless homing.
The bench changes are excluded from the production source.

The bench passed 45 checks over 874 requests, including completed-segment and
position assertions, pause/stationary/resume, replacement while paused/running,
live speed changes, all six clearing choices, twenty rapid replacements, an
immediate stop after start, manual movement, distinct-file automatic playlist
handoff, loop/next/previous/skip, playlist pause/stop, and clearing before the
second playlist entry. Full clearing completion was not awaited. Six diagnostic
503 responses recovered within bounded read-only retries. Start acknowledgements
were median 35.11 ms and maximum 75.46 ms; there was no unplanned reset.

The live bench browser captured 383 actual chip position events in 28 seconds.
The 800-pixel guide was visible, the path contained 2,025 drawn pixels, and
rendering took at most 0.3 ms per frame. SSE coordinates were a median 1.17 and
95th-percentile 2.63 canvas pixels from the guide strokes. The screenshot's
ball center matched its expected displayed position within 0.8 pixel. This
checks actual chip-to-browser behavior with no synthetic browser coordinates.

Expanded checks exposed three additional fixes: rounded brightness readback
(now all 101 percentages round-trip), bounded retries for Machine information
when startup returns 503, and stopping unsupported RHO-service polling after
404 while keeping normal manual-page status updates. Six local UI suites pass,
including focused regressions for the latter two failures.

Mixed uploads, twelve images, diagnostics and running planner traffic then
exposed an incomplete status body and a status timeout. There was no reboot or
planner queue underrun. Local inspection identified ESPAsyncWebServer 3.9.5
marking the response finished before its final buffered bytes are accepted by
TCP; a partial/zero TCP write can therefore close the response prematurely.
`KnownLengthResponse` now retains outgoing bytes through partial/zero writes,
and completes only after TCP accepts them. JSON, images, and all four HTML pages
use it. A repeated run passed 490 requests, 372 status polls, and 2,268 SSE events
without transport failures; status median was 27.61 ms and maximum 706.87 ms.
Sixty-two explicit busy responses recovered within bounded read-only retries.
Planner queue underruns remained zero. Twelve full images under this concurrent
load took 3.78–11.04 seconds.

A subsequent pacing experiment exposed another allocation panic: the response
object's inline 1 KB staging array required too large a contiguous allocation.
The retained backtrace resolved to `operator new` in `handleStatus`. The response
now has a 256-byte inline fallback and attempts an optional, nonthrowing 1 KB
allocation once. HTTP handlers catch allocation exceptions and return 503, or
abort the request if even that allocation is unavailable. Header assembly also
handles allocation failure. Native tests cover partial headers/bodies, final
zero writes, transient send failure, cancellation, TRY_AGAIN, allocation refusal,
and byte-identical delivery of all four actual HTML pages, with ASAN/UBSAN.
Final on-chip retesting passed: 354 requests, 245 successful status polls,
1,524 position events across two SSE clients, and twelve byte-identical images
with uploads and index refreshes. There were no transport failures, resets,
new motion errors, or planner underruns. Forty-two explicit busy responses
recovered within bounded read-only retries. Status latency was median 32.75 ms
and maximum 1,525.87 ms; images took 1.65–12.04 seconds under this load.
The two recorded driver errors are expected with the RHO drivers disconnected.
The final planner queue minimum was 510, and step timing reported no outliers.

Normal production firmware was restored and verified with `benchMotionTest=false`.
Saved tuning matched the pre-test snapshot exactly, speed returned to 5 and
brightness to 50, both playlist collections were empty, and all 29 original
patterns plus 27 thumbnails remained. Test fixtures were removed. Diagnostics
were archived before testing the error/log clear controls.

The final production Chromium suite passed all 31 checks across four pages.
Home became ready in 1,059 ms, manual in 313 ms, tuning in 542 ms, and files in
313 ms. Machine information populated; previews loaded on scroll, matched the
canvas sand color, and displayed no size labels. Selection took 0.5 ms. Idle
SSE and the center ball worked. The manual page requested the unsupported
RHO-service capability exactly once while continuing normal status polling.
There were no JavaScript exceptions, transport failures, or unexpected API
errors. Browser upload/abort failure checks were locally mocked; actual uploads
and emergency stop were covered by the chip suites above. Serial is released.

## Motor-connected verification

A combined run after the command-mailbox fix completed 24 rapid starts while
fetching six complete images and polling diagnostics. Start acknowledgements
were median 26 ms / maximum 83 ms; telemetry median 28 ms / maximum 183 ms.
The test asserted completed segments and actual rho position, not only HTTP
success. It observed no motion errors or reset; diagnostic requests retried
11 explicit busy responses. Later thumbnail-upload/rescan stress exposed the
index allocation crash and the separate network loss described above.

The no-motor suite verifies the live SSE overlay and ball trace, uploads/rescans,
80-character filenames, empty-file rejection, all pattern replacement/clearing
choices, and automatic playlist handoff. An active emergency stop also passed:
playback canceled, the assumed origin was invalidated, restart returned 409,
and a second stop was idempotent.

After reconnecting and power-cycling, both configured axes reported connected.
Production firmware completed sensorless homing with failure=0 and 285/285 valid
UART samples. The operator confirmed motors running and authorized interrupting
their pattern for testing. No accelerated bench firmware was used in this phase.

The connected-motor suite passed 12 checks over 174 requests: stopping the
operator's pattern, starting short two-axis fixtures, replacing a running
pattern with and without clearing, interrupting clearing with a new pattern,
pause/stationary/resume, automatic completion and handoff between two distinct
playlist files, and inserting clearing before the second entry. Start responses
were median 37.42 ms / maximum 68.20 ms; status median 28.20 ms / maximum 68.16 ms.
There were no resets, new errors, planner underruns, or tuning changes. The
commanded endpoint returned within 0.1 mm of the fixture's starting radius;
this is planner telemetry, not independent encoder measurement. Timing history
included one theta step 101 microseconds late against the 100-microsecond
reporting threshold, during the pre-test pattern epoch; rho had no outliers.
Full clearing completion was not awaited, as requested.

Both temporary fixtures were deleted, and the original empty playlist, loop and
clearing preferences were restored. Motor-test evidence is under
`/tmp/sisyphus-responsiveness/motors-connected/physical-suite.json`.

Two read-only Chromium captures then observed Spiral7 approaching the center
and following its curved path. The second 28-second capture received 383 actual
SSE events, showed the 800-pixel guide and 689 trace pixels, kept the ball at the
latest trace point, and rendered in at most 0.4 ms. Actual positions were a
median 1.45 and 95th-percentile 3.85 canvas pixels from the guide; the
screenshot ball center matched reported coordinates within 0.64 display pixel.
Status latency was median 35.35 ms and maximum 82 ms. There were no transport
failures or uncaught JavaScript exceptions. Both captures received initial
library and Machine-info 503 responses that recovered automatically; the older
browser harness's overall flag remains false because it counts even a recovered
Machine 503 as unexpected. The raw reports are preserved rather than relabeled
as wholly passing.

After browser validation the table was stopped and verified IDLE, with zero
velocity, no errors or planner underruns, unchanged tuning, speed 10 (the
operator's setting on entry), 29 original patterns, and the original empty
playlist preferences. Serial was not needed for this connected-motor phase.

Session measurements, serial captures, and host scripts are under
`/tmp/sisyphus-responsiveness/`. `deployed-sleep-default.elf` matches the index
allocation panic; `panic.elf` matches the earlier cbuf allocation panic. Retain
the matching ELF for each firmware image when decoding additional failures.

`deployed-final.elf` matches the earlier core-isolation build; its measurements
are in `final-usb-image-stress.json`, `core0-bulk-stress.json`, and
`ui-readonly-report.json`. The expanded suite artifacts are in `no-motor/`:
`api-suite.json`, `bench-suite.json`, `bench-live-browser-report.json`,
`load-final.json`, and `emergency-stop.json`. `bench-four-chunks.elf` matches the
response-object allocation panic; `bench-final.elf` and `production-final.elf`
match the corrected bench and production builds.
