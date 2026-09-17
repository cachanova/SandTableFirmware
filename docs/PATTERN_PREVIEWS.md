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

PNG-only uploads now replace the image beside an existing flat THR. Previously
those uploads were written to a nested directory and the index still served the
old flat PNG. Full-image replacement invalidates the nested thumbnail in either
layout. Uploads retain staging and rollback behavior.

## Refreshing a library

Use a new output directory for each backup. The utility copies every THR and any
existing PNG/thumbnail, records SHA-256 hashes, renders locally, then uploads only
successful PNG/thumbnail pairs. It rejects changed source metadata and compares
every uploaded asset byte-for-byte with a fresh device read. Invalid or empty THRs
are retained and reported; no THR or playlist is rewritten.

```sh
python scripts/refresh_previews.py download --output /tmp/table-preview-backup
python scripts/refresh_previews.py render --output /tmp/table-preview-backup \
  --renderer ../ThrGenCpp-preview/build/ThrGenCLI
python scripts/refresh_previews.py upload --output /tmp/table-preview-backup
```

The backup contains per-pattern `before.png`, `before.thumb.png`, and the original
THR, plus three stage manifests. Retain it until deployment validation is complete.
