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
  byte for byte. ESPAsyncWebServer 3.9.5 consumes in-flight
  credits even on `RESPONSE_TRY_AGAIN`, which can permanently stall background
  streams. Its optional credit gate is disabled; TCP still provides backpressure.
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
initial navigation failed to initialize within 20 seconds; a fresh retry passed.
This transient first navigation is not yet explained.

## Motor-connected verification still required

A combined run after the command-mailbox fix completed 24 rapid starts while
fetching six complete images and polling diagnostics. Start acknowledgements
were median 26 ms / maximum 83 ms; telemetry median 28 ms / maximum 183 ms.
The test asserted completed segments and actual rho position, not only HTTP
success. It observed no motion errors or reset; diagnostic requests retried
11 explicit busy responses. Later thumbnail-upload/rescan stress exposed the
index allocation crash and the separate network loss described above.

USB testing is complete and serial has been released for motor reconnection.
With the rho driver absent, firmware correctly refused homing. Remaining checks:

1. Restore motor connections, allow normal homing, then verify replacing a
   running pattern with and without clearing and automatic playlist handoff.
2. Verify actual live SSE, guide overlay, and ball trace in Chromium. The local
   render above does not substitute for this check.
3. Validate upload/rescan, an 80-character filename, and empty-file rejection.
   Remove `codex_response_probe.thr` and any further test-only files. Restore
   the original empty playlist with loop disabled and clearing enabled.

Session measurements, serial captures, and host scripts are under
`/tmp/sisyphus-responsiveness/`. `deployed-sleep-default.elf` matches the index
allocation panic; `panic.elf` matches the earlier cbuf allocation panic. Retain
the matching ELF for each firmware image when decoding additional failures.

`deployed-final.elf` matches the final USB-tested build;
`final-usb-image-stress.json`, `core0-bulk-stress.json`, and
`ui-readonly-report.json` contain the corresponding measurements.
