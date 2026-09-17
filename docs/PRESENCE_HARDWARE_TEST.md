# Presence hardware test attempt — 2026-09-16

After testing, main advanced to `47eb654` with sound-profile documentation only,
including the selected 200 mA RHO hold setting. The presence branch was rebased
again onto that main. No firmware, script, or test source changed in this final
rebase; the installed image is unchanged. Deployment commit IDs below are the
pre-rebase IDs; the final installed source is now `77da374` (formerly `f0d965e`).

## Recovery and follow-up (supersedes the initial offline state)

The operator power-cycled the table. The presence firmware came online, automatic
homing succeeded, and live CSI was received. An operator-started clearing pass
for `shell.thr` was left alone until the operator explicitly requested stopping
it. The stop completed; sensing resumed after its five-second settling delay.
Motion telemetry for that observed run reported zero planner underruns. Tuning
and library entries matched the pre-install snapshots.

The first presence load attempt stopped after about 24 seconds because the
monitor could not recover from busy responses. The device did not reboot or log
an error, but byte-addressable minimum heap reached **852 B**. Sensing's memory
pause and recovery were observed. This was a failed resource-margin test, not a
successful stress qualification.

Commit `9b8fff6` corrects image and index admission to use byte-addressable free
heap, and image admission also checks for an 8 KiB contiguous block. Its OTA
completed at 22:32:45 US/Central and reboot/homing/reception succeeded without a
power-cycle. A first conservative 20,000-byte current-heap cutoff stopped at
18,372 B, with a 10,628 B lifetime minimum. The harness was then changed to allow
the designed 16/24 KiB pause/resume hysteresis while enforcing an 8 KiB current
and new transient low-water floor. The next run failed that stronger transient
check at **3,292 B**; the device again recovered without logged errors or reset.

Commit `14b05d4` serializes full HTML/image responses using a disconnect-released
bulk-response slot, with a 24 KiB byte-heap admission reserve. Controls and SSE
are excluded from that gate; busy bulk responses return HTTP 503/Retry-After.
This improved the next measured transient minimum to **6,676 B**, but still
failed the chosen 8 KiB stress floor. These abbreviated runs must not be counted
as passes. Commit `f942369` therefore reduces image prefetch from 4 KiB to 1 KiB
(matching the existing transport/SD chunk size), leaving all task stacks intact.
That follow-up still stopped at a reported **6,484 B** minimum. Commit `f0d965e`
reclaims another 6,400 B of DRAM by retaining 32 recent console messages instead
of 64. USB serial still emits every message, the separate error history is
unchanged, and clients can poll console entries by ID. The actual log class
passes ASan/UBSan tests for overwrite, dropped counts, cursors, zero limit,
clear-with-monotonic-IDs, and full serial retention. No task stack was reduced.

**Interpretation of low-water values:** the pinned SDK documents that
`heap_caps_get_minimum_free_size` sums per-region minima which may have occurred
at different times. These values are conservative all-time lower bounds, not
proof that the total heap simultaneously reached exactly the reported number.
The harness intentionally uses them conservatively alongside current-free and
largest-block checks. No OOM crash was observed in these stopped attempts.

The final production image (`f0d965e`) uses 97,864 B static RAM and 1,441,313 B
flash, leaving 131,551 B in the application slot. This is 5,232 B less static RAM
than the archived deployed main image despite adding presence sensing; the
tradeoff is the explicitly shorter console history. Runtime allocations remain
additional. Production and OTA builds pass after this last change. Uploaded
image SHA-256:
`6c13726c8148fffdd01ef376d134ec25614b25e8e63adf9d91f4494cca582677`.
### Final bounded load result: reserve criterion still not met

The `f0d965e` upload completed at 22:52:44 US/Central. Warm reboot, automatic
homing, CSI reception, and another functional calibration all succeeded. The
requested 180-second multi-client test stopped after about 62 seconds of load
(68.3 seconds including recovery) when the conservative minimum-byte-heap
reading fell below the chosen 8 KiB floor, to **5,764 B**. **This is not a full
stress-test pass.** No further load escalation or reserve-threshold relaxation
was performed.

- 261 requests were recorded, including cleanup; 46 were retryable HTTP 503s.
  Both SSE clients received 61 events. No reboot, transport failure, or new
  device error was observed.
- All 28 successful image downloads were byte-consistent, and their SHA-256
  values matched the earlier installed-main baseline. Successful image requests
  had median 1.48 s, p95 2.57 s, maximum 3.14 s.
- The 59 successful status requests had median 40.39 ms, p95 94.28 ms, maximum
  118.94 ms. Busy responses are excluded from these latency statistics.
- Current byte-heap samples never fell below 18,428 B, and sampled largest free
  blocks never fell below 12,788 B. These intermittent samples do not disprove a
  shorter unsampled low. The conservative per-region minimum is not an exact
  simultaneous free-heap measurement, as explained above.
- Current byte-addressable free heap returned exactly from 37,416 B to 37,416 B
  at the before/after checkpoints. Largest byte-addressable block went from
  34,804 B to 32,756 B. This does not establish long-term fragmentation safety.
- Accepted presence samples rose from 168 to 1,118. Twelve queued frames were
  dropped under load; the queue remained bounded and fresh reception recovered.
  The lifetime maximum presence-loop wall time reached 123,020 us (includes
  scheduling/SDK waits, not a pure CPU benchmark). Logged spare task stacks were
  W/M/F 5,980/2,232/2,964 bytes; the image worker's observed spare stack reached
  712 bytes. No stack size was reduced.

The device was left IDLE, receiving/calibrated, with `fade_light_on` selected and
both brightness target and actual output at 50%. The library is unchanged. An
earlier tuning comparison matched; at the final comparison, only RHO hold current
had changed from 350 to 200 mA. This session sent no tuning-write requests, so
that current value was preserved rather than overwritten. Concurrent main commit
`47eb654` subsequently documented that selected 200 mA hold setting.

The unresolved qualification is a sustained heavy multi-client run meeting the
8 KiB conservative reserve criterion, plus longer soak/reconnect and actual
empty-room accuracy checks. Do not advertise immunity to runtime OOM or claim
the full 180-second stress request completed. Artifacts for this result are
`log-reserve-load.json`, `log-reserve-ota.log`, `log-reserve.bin`, and
`log-reserve.elf` under `/tmp/sisyphus-presence-install.ok5QPm/`.

### User-selected brightness

The user requested that presence fade only to the main-page brightness setting.
Commit `14b05d4` separates that target from live PWM output. Automatic fade writes
never overwrite the target; the main slider follows the target, status retains
the actual output, and the settings API returns the chosen target instead of
100. A zero setting remains off. The existing RAM-only brightness behavior is
preserved: reboot starts at 50%. No automatic dim/off behavior was added.

Host tests with the real LED/automation classes verify a 40% fade ceiling,
target retention across intermediate writes, zero/off, target changes, and
Arduino 2/3 PWM-call branches under ASan/UBSan. Native automation tests also
cover timer rollover and cancellation; browser tests cover separate target/live
values and zero. All seven browser files, 104 Python tests, and the native
synthetic suite pass. All eleven firmware environments built after the brightness
and bulk-response changes; production/OTA were rebuilt after the prefetch change.

On the device, POST brightness values 37%, 0%, and 50% read back identically from
status, LED GET, and presence-settings GET. Calibration completed with 160 samples
(about 15 seconds); later actual motion detections left the selected 50% output
unchanged instead of raising it to 100%. The room's empty state was not confirmed,
so this demonstrates functional calibration, not sensitivity/false-positive
qualification. The latest image remains production firmware, not a bench mode.

## State of verification

The following describes the **initial** installation attempt, not current
connectivity; see the recovery section above.

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
