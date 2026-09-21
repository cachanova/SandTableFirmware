# Flash footprint

The `esp32dev` image had grown to 95.1% of its 1.5 MB app partition
(1,496,165 of 1,572,864 bytes), leaving 76 KB. Both OTA slots are that size
and the partition table already spends the rest of the 4 MB part, so the
headroom had to come out of the image.

## Where it was going

Sorting the ELF by symbol size put the answer first:

| Symbol | Bytes |
| --- | --- |
| `WEB_UI_SCRIPT` | 76,022 |
| `WEB_UI_HEAD` | 18,982 |
| `SETTINGS_UI_SCRIPT` | 16,973 |
| `SETTINGS_UI_BODY` | 16,734 |
| `MANUAL_UI_SCRIPT` | 15,205 |
| ...the rest of the pages | ~58,000 |

The four UI pages held 202,663 bytes of flash -- 13% of the partition and
roughly half of `.flash.rodata` -- as plain HTML, CSS and JavaScript.

## What changed

`scripts/build_ui_gz.py` runs as a PlatformIO pre-script. It joins each page
from the spans in `lib/WebServer/src/*UI.h` the way `UIPages.hpp` used to at
send time, gzips the result, and writes the blobs to the generated
`lib/WebServer/src/UIPagesGz.h`. `StaticContentResponse` sends them with
`Content-Encoding: gzip`; the browser inflates them.

| | Before | After |
| --- | --- | --- |
| Pages in flash | 202,663 B | 47,765 B |
| Image | 1,496,165 B (95.1%) | 1,340,985 B (85.3%) |
| Free | 76,699 B | 231,879 B |

The pages stay authored as readable HTML -- nothing about editing them
changes, and the `.cjs` UI tests still read the same literals. Sharing the
dialog spans across pages no longer pays anything, because each page is now
one deflate stream, so the generator splices them instead.

The pages are sent gzipped unconditionally: there is no uncompressed copy to
fall back to, so checking `Accept-Encoding` could only change how a client
without it fails. Every browser sends it. The repository's own diagnostics use
`urllib`, which does not, but they read only status codes and lengths --
`http_diagnostics.py` through `get_with_status`, `presence_stress.py` by
comparing `Content-Length` against the body it received.

Adding the header allocates, so `StaticContentResponse` adds it from
`_addResponseHeaders` inside the guarded respond path rather than from its
constructor, where a refused allocation would escape the route handler.

`test/test_ui_gzip.cjs` inflates every blob and compares it byte-for-byte
against the page sources, reruns the generator to confirm the output is
reproducible, and fails if the four pages ever exceed 64 KB of flash.

## Candidates not taken

- **`-Os` instead of `-O3`.** A probe environment built byte-identical to the
  `-O3` image, so the flag was not reaching the compiler; whatever the real
  number is, the motion path is the reason to be careful here, and 226 KB of
  headroom means nobody has to find out yet.
- **Newlib nano formatting.** `_vfprintf_r`, `_svfprintf_r`, `_vfiprintf_r`,
  `_svfiprintf_r` and `__ssvfiscanf_r` total about 12 KB, and both the float
  and integer-only variants are linked in. The Arduino framework ships
  prebuilt, so selecting nano means rebuilding it.
- **Growing the app partitions.** They are already 0x180000 each and the rest
  of the 4 MB is `nvs`, `otadata` and the 960 KB filesystem. Repartitioning
  would break OTA from any deployed image.
