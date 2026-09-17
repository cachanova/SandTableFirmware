# Web-load heap reserve — 2026-09-17

Follow-up: the four-buffer setting below is superseded by the six-buffer receive
budget and 28 KiB bulk-admission threshold in [PATTERN_PREVIEWS.md](PATTERN_PREVIEWS.md).
Unrestricted uploads exposed a receive stall that the GET/SSE qualification below
did not exercise.

This work starts from main `f8dda0e`. The firmware changes are `5666be9` and `2319954`.
The required byte-addressable heap reserve remains **8,192 bytes**, with a
**4,096-byte** sampled largest-block floor. Task stacks, motion queues, image
prefetch size, and the existing 24 KiB bulk-admission threshold are unchanged.

## Cause and correction

Ordinary once-per-second telemetry missed brief memory peaks. Temporary
allocation wrappers recorded a burst of 2,308-byte allocations in the Wi-Fi
task: actual free byte-addressable heap reached **5,392 bytes**, while the SDK
lifetime minimum reached **5,168 bytes**. See the final allocation ring in
[allocation-evidence.json](web-heap/allocation-evidence.json). This confirms an
actual low reserve, beyond the SDK minimum's conservative aggregation of
separate heap-region minima.

`WifiMemory.cpp` now limits dynamic receive buffers to four, down from twelve,
and keeps the receive block-ack window at four. Static receive buffers drop
from six to four; transmit buffers remain six. This returns about 3 KiB of
permanently reserved DMA memory as well as limiting dynamic receive bursts.
The dynamic limit is at least the static receive count, as recommended by the
[ESP-IDF 4.4.7 configuration guidance](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/kconfig.html#config-esp32-wifi-dynamic-rx-buffer-num).
This bounds receive bursts while leaving the existing TCP receive window usable.

`KnownLengthResponse` caps accepted but unacknowledged bytes at two SDK TCP
segments: **2,872 bytes** for the pinned toolchain, rather than filling the
5,760-byte TCP send buffer. Headers and body share that allowance; polls cannot
replenish it. Small responses of at most 256 bytes use the inline staging buffer
without allocating another 1 KiB block. Partial writes and allocation-failure
fallback retain exact response bytes.

A transport-only trial still failed at 5,952 bytes. Reusing a persistent image
worker also failed, at 4,512 bytes, and was removed. The final firmware retains
the existing image worker lifecycle. Allocation tracing and its HTTP endpoint
were removed before the production build; the final ELF was checked for absence
of the tracing symbols.

## Validation

The diagnostic build with an intermediate six-buffer receive budget completed
the 180-second presence load: 734 requests, two SSE streams with 174 events each,
four stable PNG identities, no reset or device errors. Its lifetime minimum was 11,304 bytes;
free byte heap recovered from 36,716 to 36,692 bytes. Tracing itself used extra
RAM. The corresponding normal firmware passed idle load at 9,056 bytes but
failed concurrent motion/web traffic at 7,712 bytes. That prompted the final
four-buffer receive budget, which is qualified separately below.

The final normal firmware completed the 180-second stationary load (186.3
seconds including cleanup): 716 recorded requests, 143 retryable 503 responses,
64 complete PNG downloads, and 175/174 SSE events. The four PNG hashes match
the pre-fix firmware. Lifetime minimum byte heap was **12,800 bytes**; sampled
free heap stayed at or above 21,220 bytes and sampled largest blocks at or above 17,396
bytes. Byte heap recovered from 43,412 to 42,908 bytes. Status latency was
34 ms median / 65 ms p95, with a 1.17-second worst case. PNG request attempts
(including busy replies) had 1.50-second median / 3.87-second p95 latency.

On the same boot, the complete example circle finished in 87.7 seconds under
two SSE clients, image/SD reads, and telemetry polling. It produced 2,298 SSE
events across 212 telemetry samples, zero planner underruns, and the expected
commanded endpoint (rho 212.5 mm, theta 6.280044 rad). Sampled ball speed stayed
below 30 mm/s. Neither axis had a step-deadline outlier above 100 microseconds
(theta maximum lateness 88 us, rho 83 us); five timer-callback gaps were recorded,
with a 113 us maximum. The lifetime byte-heap minimum remained **12,800 bytes**.

Finally, 20 interrupted PNG downloads each recovered to a complete, stable
image on the same boot (49.2 seconds). The cumulative lifetime minimum stayed
at **12,800 bytes**, 4,608 bytes above the requirement. The table ended idle;
presence reception resumed; saved tuning, speed 5, brightness 50%, the 29-file
library, playlist, and error log matched the starting snapshots. The temporary
circle fixture was removed. Presence remains uncalibrated, as before this work.

The installed four-buffer firmware also successfully received a full OTA update
in 29.83 seconds, then booted and homed. Hardware results and the installed image
hash are recorded in [hardware-validation.json](web-heap/hardware-validation.json).

Host checks cover ACK-bound pacing, zero/partial writes, polling without ACKs,
small responses, refused staging allocation, cancellation, header allocation
failure, and byte-for-byte delivery of all four HTML pages under ASan/UBSan.
The response-buffer tests, 107 Python tests, and all seven browser test files
also pass. Normal production and OTA builds pass. Static RAM is unchanged at
95,368 bytes; application flash is 1,465,965 bytes of the 1,572,864-byte slot.

The load script now rejects an already-failed lifetime minimum before starting,
checks it throughout the run, and checks recovery after fetching diagnostics.
A recovered current heap can no longer hide a previous failure on the same boot.

```sh
# Begin after a fresh normal boot, successful homing, and live presence samples.
python scripts/presence_stress.py --duration 180 --presence
# On the same boot, interrupt PNGs and verify complete downloads recover.
python scripts/web_cancel_stress.py --cycles 20
```

Raw reports, OTA logs, and the installed binary/ELF are under
`/tmp/sisyphus-web-heap/` on the development machine. The JSON evidence preserves
summaries and binary identity independently of that temporary directory.
These checks qualify the recorded workloads on this table; they do not establish
an unlimited-client capacity or AP-reconnect qualification.
