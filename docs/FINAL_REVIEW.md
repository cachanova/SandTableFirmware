# Final capability and memory review

Reviewed the production web routes, playback/clearing/playlist transitions,
manual and tuning controls, SD reader/index/image/upload tasks, response
ownership, persistence, telemetry, and browser request/render lifetimes.
The planner's segment queues, diagnostic rings, and image prefetch stream are
already bounded. Networking remains on core 0 and motion on core 1.

## Fixes

- Response builders now own responses until handing them to the server. If a
  header or serialization allocation throws, the response and any image worker
  are released. Cache and diagnostic locks unwind on failure; diagnostic output
  is serialized after releasing the motion lock. Request admission counters are
  published only after their disconnect callback is installed.
- Queued motion and background pattern reads handle allocation refusal instead
  of allowing an exception to terminate the task. Relative jog allocation also
  releases its motion lock. The pattern reader checks pending commands between
  4 KB reads, so a malformed very long line cannot postpone a replacement until
  the entire line is consumed. File stdio buffering is limited to 512 bytes.
- JSON responses reject overflowed documents. Tuning and playlist saves check
  document completeness, exact serialized length, flush/write errors, and staged
  file size before replacing the original. Playlist storage uses segmented
  allocation; reorder rotates existing entries without allocating copies.
  Playlist loads refuse files over 32 KB, and resetting the public cursor to -1
  no longer creates an invalid negative next-item index.
- Multipart upload callbacks have their own allocation guard because they run
  before request middleware. Interrupted uploads remove staging files on
  disconnect. Extra file parts cannot overwrite/leak the original upload
  context. Upload commit also checks flushed size and write errors. Under an
  allocation failure during cleanup, an ignored staging file can remain on SD;
  this does not replace the original file.
- Deletion handles both legacy flat files and nested pattern directories,
  including a flat pattern with a thumbnail-only directory. Partial deletion
  reports failure and refreshes the index instead of claiming success.
- Manual-page status and geometry polling now share in-flight requests and have
  four-second deadlines covering response bodies.
- System information exposes `largestFreeBlock` and `minimumFreeHeap` alongside
  total free heap, making fragmentation visible without serial diagnostics.

## Trace gaps

The browser previously discarded all queued positions after 128 updates while
animation frames were paused in a background tab. At the normal telemetry rate,
that could lose about eight seconds of trace at a time. It now rasterizes each
bounded batch into the existing path canvas while leaving ball/text painting
for the next frame. Memory stays bounded and every received segment survives.
Actual SSE disconnections still break the trace: the device does not replay
positions missed while disconnected, and drawing a straight bridge would
misrepresent the traveled path.

## Verification

- Six browser regression suites pass, including 5,000 updates with animation
  frames withheld: all trace segments survive, pending samples never exceed
  128, and only one animation callback remains pending. Stalled manual status
  and geometry bodies time out and subsequent polls recover.
- A real Chromium session loaded the changed HTML locally and used the running
  chip's actual GET/SSE endpoints without restarting or stopping it. With
  animation callbacks deliberately suspended for 20 seconds, it received 271
  positions, rasterized 254 segments before resuming, and finished with 269
  non-stationary segments, no backlog, and ball/trace coordinates equal. The
  overlay remained visible. This verifies changed browser code against live
  telemetry; it does not mean changed firmware was deployed.
- ASAN/UBSAN persistence tests use the actual ArduinoJson library: partial JSON
  allocation, short writes, flush errors, and stale staged contents are refused;
  complete compact and pretty JSON round-trip.
- ASAN/UBSAN pattern-reader tests cover CRLF/refill/EOF, oversized lines without
  accidental coordinate fragments, and command interruption after 8 KB of a
  one-megabyte line. Existing response-buffer and twelve transport fault groups
  also pass under ASAN/UBSAN.
- The production firmware build, synthetic motion suite, and all 24 repository
  pattern files pass. Actual playlist-manager tests with an in-memory filesystem
  cover cursor reset, reorder in both directions, the 256-item cap, saved-file
  preservation on short write/flush/rename failure, and rejection of invalid or
  oversized loads without changing the live playlist.

## Production deployment and device verification

Installed commit `3d3163e` on `100.76.149.200` over OTA on 2026-09-16 after the
operator authorized interruption. The production image SHA-256 is
`9b165aa5319443a1cb8a89e7d3ff1b6482c7915eb83d275a0e528423be054778`.
OTA completed in 32.6 seconds. Automatic homing succeeded with both fitted
axes connected; the unused rho companion remains disabled. This is normal
production firmware, with bench mode false and no commissioning axis.

- Thirteen connected-motor checks passed across 186 requests: relative jog,
  pattern start/replacement with and without clearing, pause/stability/resume,
  short-pattern completion, automatic two-pattern playlist handoff, and clearing
  between playlist entries. Median start/stop command response was about 37 ms;
  median status response was 27 ms. No reset, logged error, or planner underrun
  occurred. Positions are planner coordinates, not independent encoder feedback.
  Full clearing completion was not awaited.
- Saving the unchanged motion settings succeeded and all tuning read back
  identically to the pre-install snapshot. Speed was restored to 10.
- Interrupted image replacement preserved the original PNG; oversized thumbnails
  were rejected, canceled downloads released their requests, and subsequent
  uploads succeeded. A second multipart file was rejected without leaking its
  context; the already committed first file remained, as intended by the
  single-file upload protocol. Playlist reorder in both directions and removal
  also passed.
- Twelve full-resolution images matched their source bytes while uploads,
  diagnostics, status polling, and two SSE clients ran concurrently. The streams
  received 1,827 events with no transport failures or reboot. Admission control
  returned 39 temporary busy responses, which recovered on retry. Large image
  downloads still took 2–13 seconds under this load; this is not a claim of
  instantaneous full-resolution image transfer.
- Chromium then loaded HTML directly from the updated chip. With animation
  callbacks withheld for 20 seconds, all 241 received trace segments survived;
  pending samples stayed at or below 128, the resumed backlog was zero, and ball
  coordinates equaled the trace endpoint. The 800-pixel guide stayed loaded and
  no JavaScript exception occurred. This deliberately pauses animation callbacks;
  it does not simulate a network disconnect or reconstruct missed telemetry.
- Final idle free heap was 75,712 bytes, largest free block 38,900 bytes
  (unchanged from the post-boot baseline), and minimum free heap 41,544 bytes.
  These short tests found no continuing allocation loss; they are not a long
  duration leak proof.
- Test files were removed, the original library and empty playlist restored,
  and the table stopped in IDLE. On-device errors and planner underruns were zero.

Deployment artifacts and reports are under
`/tmp/sisyphus-final-review/install/`. Early host-harness checks were corrected
for the jog endpoint's expected HTTP 202 and retryable file-list HTTP 503;
completed reports above use those endpoint semantics.

Host artifacts for this pass are under `/tmp/sisyphus-final-review/`.


Reproduce the focused native additions after PlatformIO installs dependencies:

```sh
cxxflags="-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined"
g++ $cxxflags -Ilib/Config/src -I.pio/libdeps/esp32dev_ota/ArduinoJson/src \
  test/test_json_persistence.cpp -o /tmp/test_json_persistence
/tmp/test_json_persistence
g++ $cxxflags -Ilib/PolarControl/src test/test_pattern_line_reader.cpp \
  -o /tmp/test_pattern_line_reader
/tmp/test_pattern_line_reader
g++ $cxxflags -Wno-sign-compare -Itest/support/playlist -Ilib/Playlist/src \
  -Ilib/Config/src -I.pio/libdeps/esp32dev_ota/ArduinoJson/src \
  test/test_playlist_memory.cpp lib/Playlist/src/PlaylistManager.cpp \
  -o /tmp/test_playlist_memory
/tmp/test_playlist_memory
```
