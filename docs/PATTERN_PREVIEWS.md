# Faithful THR previews

The renderer in SandTablePatternGen now samples unwrapped theta/rho segments before
Cartesian projection. Complete turns and polar curves are retained. Floating-point
coordinates map directly to the final 800-pixel overlay (center 400, radius 380),
avoiding the old double truncation offset. PNGs, 128-pixel thumbnails, GIFs, and
browser path previews use the serialized THR geometry. Raster chord error is bounded
to 0.05 pixels before antialiasing. Approach moves, physical ball tracking, and the
firmware's bounded corner smoothing are outside the preview model.

`GET /api/pattern/download?file=Name.thr` exports the exact installed source. It
accepts an indexed simple THR filename, supports flat and nested layouts, sets
`Cache-Control: no-store`, and shares PNG concurrency, heap admission, cancellation,
and bounded asynchronous SD reads. It never allocates the entire file in RAM.
Empty indexed files return an empty 200 response; invalid names return 400 and
missing names 404 (503 while the index is stale or resources are busy).

THR and PNG uploads preserve an existing flat layout. Previously PNG-only updates
were written to a nested directory and the index still served the old flat PNG.
Keeping THR replacements flat also lets the following PNG upload use the same
layout before the index refreshes. Full-image replacement invalidates the nested
thumbnail in either layout. Uploads retain staging and rollback behavior.

## Refreshing a library

Use a new output directory for each backup. The utility copies every THR and any
existing PNG/thumbnail, records SHA-256 hashes, renders locally, then uploads only
successful PNG/thumbnail pairs. It rejects changed source metadata and compares
every uploaded asset byte-for-byte with a fresh device read. Invalid or empty THRs
are retained and reported; no THR or playlist is rewritten.

```sh
python scripts/refresh_previews.py download --output /tmp/table-preview-backup
python scripts/refresh_previews.py render --output /tmp/table-preview-backup \
  --renderer ../ThrGenCpp/build/ThrGenCLI
python scripts/refresh_previews.py upload --output /tmp/table-preview-backup
```

If a download is interrupted, repeat its command with `--resume`. Completed files
are hash-checked and skipped only if the original library metadata still matches.
Uploads also support `--resume`: source metadata must still match, and every
completed asset is checked against its local hash and a fresh device read before
it is skipped. Changed previously verified assets stop the resume for review.
The utility sends bounded pieces under the original multipart Content-Length, so
its socket timeout applies to stalled progress rather than the whole file write.

The backup contains per-pattern `before.png`, `before.thumb.png`, and the original
THR, plus three stage manifests. Retain it until deployment validation is complete.

## Upload acceptance and failure handling

Multipart callbacks now write only a staging file. The final request handler
requires exactly one `file` part, nonempty content, the original declared HTTP
length, and a complete MIME closing boundary before promotion. Empty files,
extra file/form parts, and malformed or abandoned requests leave the old file in
place. A narrow reproducible patch to pinned ESPAsyncWebServer 3.9.5 exposes and
checks multipart completion; `scripts/patch_async_upload.py` runs after dependency
resolution and refuses an unfamiliar parser layout rather than silently patching it.

Only one upload owns the staging admission slot. Index scans yield to active
uploads, and upload admission yields to a running scan. Disconnect cleanup releases
the slot and removes staging. Failed promotion restores the old file; a failed
rollback retains its `.bak` recovery file. An unexplained existing backup refuses
replacement (409). A successfully replaced THR invalidates the associated PNG and
thumbnail, preventing a partial client upload from showing the previous pattern's
preview. A PNG replacement invalidates only its thumbnail.

These are per-file transactions. The browser presents separate pattern, preview,
and thumbnail receipts; a multi-file transfer can finish partially and be retried.
An interrupted acknowledgement can leave a committed file, so clients report an
unconfirmed outcome and can idempotently resend. The firmware continues to validate
THR geometry before playback; upload acceptance itself is not a playback guarantee.

## Upload burst receive starvation

The previous four-buffer dynamic Wi-Fi RX limit passed GET/SSE loads but repeatedly
stalled unrestricted multipart PNG uploads. A 29,494-byte PNG succeeded when paced
at 1 KiB/s; at normal speed its receive callback stopped at 8,248 bytes and HTTP/ARP
became unreachable. Temporary RTC-backed diagnostics showed the controller kept
running: minimum byte-addressable heap was 24,316 bytes, largest blocks remained
above 27 KiB late in the stall, and both failed-allocation and failed-Wi-Fi-TX counts
were zero. This was a receive-path stall, not a panic or demonstrated heap exhaustion.

Changing only the dynamic RX limit from four to six allowed the identical normal-speed
upload to commit completely. Its diagnostic minimum heap was 26,232 bytes, with no
allocation or transmit failures. Static RX remains four, block-ack window four, and
static TX six. The measurements establish that the four-buffer configuration caused
the failure on this device; receive-buffer starvation is the inferred mechanism.

Production bulk admission now requires 28 KiB of free byte-addressable heap, adding
4 KiB of headroom for the extra receive capacity. New bulk responses defer while an
upload is active, and uploads also check this memory threshold before opening their
staging file. Small control requests remain available. Temporary diagnostic hooks
and timed recovery resets are removed from the final firmware.

## Image replacement and response lifetime

Image requests now defer while the index is dirty, including when the old entry
still exists. Otherwise a newly replaced PNG could be served with its previous
cached length. An observed replacement contained 331,637 bytes but was advertised
as 332,223 bytes. Uploads also defer while an image/source stream is active.

The resulting premature EOF exposed a separate lifetime bug: the custom response
closed its client synchronously inside `_ack()`, destroying both itself and its
request. ESPAsyncWebServer then checked the freed response again. The device
backtrace identified `_onAck()` at this recheck, and an AddressSanitizer regression
reproduced the use-after-free with the original code. Failed responses now mark
their state and arm a one-second inactivity timeout. The SDK closes them after
the response callback returns, including failures with no further ACK pending.

Large uploads use a 30-second inactivity timeout instead of the SDK's three-second
default. Completed SD writes feed the AsyncTCP task watchdog and yield one tick;
one network event can otherwise spend several seconds processing many file chunks.
These changes retain timeout protection for stalled work rather than imposing a
total transfer deadline.

`GET /api/system/crash` retains this boot's previous panic frames after the console
ring wraps, includes watchdog resets, and reports the running firmware's MD5.
Match that fingerprint to the uploaded binary before decoding addresses with its
ELF. The endpoint does not initiate a reset or enable diagnostic auto-restarts.

## Long SD operations

Removing an archived 34,716,452-byte truncated legacy THR triggered a task watchdog
reset while the same firmware was running. The entry was absent after reboot.
The synchronous delete path can walk a large FAT cluster chain without returning
to the HTTP task; feeding only between upload chunks does not cover that work.

`src/SDProgress.cpp` wraps registration of the pinned Arduino SPI SD callbacks.
After a successful sector read or write returns and releases its SPI lock, it
services the current task's watchdog if subscribed and yields at most once per
20 ms. Failed or stuck disk I/O does not receive a watchdog reset. Other disk
drivers and callback registration/unregistration retain their SDK behavior.
The framework installation is not modified, and watchdog timeouts remain enabled.

The callback regression checks forwarding, error handling, unsubscribed tasks,
yield throttling, and tick rollover:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Itest/support/sd_progress test/test_sd_progress.cpp -o /tmp/test_sd_progress
/tmp/test_sd_progress
```
