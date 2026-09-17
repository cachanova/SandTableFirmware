# Presence memory and performance checks

The measurements below describe the earlier 2026-09-16 review against main
`f39a7ea`, when nothing was flashed. For the subsequent rebase onto `eeb368d`,
updated build sizes, baseline load test, and OTA/startup outcome, see
[PRESENCE_HARDWARE_TEST.md](PRESENCE_HARDWARE_TEST.md). Do not treat the earlier
host checks as a successful on-device qualification of presence sensing.

## Build comparison

Both production images use the same pinned PlatformIO/Arduino/IDF toolchain.
Main was rebuilt in an isolated detached worktree: concurrent uncommitted edits
in the primary checkout were left untouched and excluded from this comparison.

| Resource | Main `f39a7ea` | Presence branch | Difference |
| --- | ---: | ---: | ---: |
| Static RAM | 103,064 B | 104,224 B | +1,160 B |
| Application flash | 1,411,217 B | 1,438,849 B | +27,632 B |
| Remaining application slot | 161,647 B | 134,015 B | −27,632 B |

The image uses 31.8% of reported static RAM and 91.5% of the OTA slot. Static
RAM excludes runtime task stacks, Wi-Fi/lwIP buffers, HTTP allocations, and
allocator overhead. It is not a measurement of free runtime heap.

## Allocation limits

- The target ELF gives `PresenceSensor` a size of 1,148 bytes. This includes
  its 304-byte detector, four 144-byte capture slots (576 bytes), and 84 bytes
  each for the queue and mutex controls. These subobjects overlap that total.
  Static queue/mutex allocation replaces the old sixteen-slot heap queue,
  whose payload alone occupied 2,304 bytes.
- There is no queue growth, stored frame history, or allocation per classified
  sample. The linked detector references only arithmetic, memcpy/memset, and
  sqrt helpers; the automation library has no undefined external symbols.
- The SDK ping task still uses runtime heap: a 2,560-byte stack and 344-byte
  task control structure on this SDK, plus its session, 40-byte ICMP packet,
  socket/lwIP bookkeeping, and allocator overhead. These are additional to the
  static delta. Creation is deferred until the web loop, after the motor/web
  tasks have been created; no SDK stack was reduced.
- Motion, settling, and memory pressure pause ping transmission and capture.
  Those pauses reuse the ping task/stack/socket. AP/channel/gateway changes
  retire the session and defer replacement at least five seconds; failed
  allocation/admission retries are rate-limited too. SDK deletion is
  asynchronous, so delayed cleanup can still cause transient overlap.
- Once per second, sensing checks free 8-bit heap and its largest free block.
  Below 16 KiB free or a 4 KiB largest block, sensing/calibration/automation
  suspend. Resumption requires 24 KiB free and a 4 KiB block. New ping sessions
  repeat the stricter check immediately before allocation. This is conservative
  admission control, not an atomic reservation against other tasks allocating.
- Presence GETs now use main's `BufferedResponse`: bounded 1 KiB blocks with
  allocation refusal near the 16 KiB bulk-response reserve. Settings HTML uses
  `StaticContentResponse`. The growing Arduino `cbuf` is not reintroduced.
  Main's allocation-failure middleware, partial-write fixes, and reserved
  status/control-response budget are preserved.
- Re-saving an unchanged action skips NVS writes.

Ping allocation/lifecycle was checked against the
[pinned ESP-IDF implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/lwip/apps/ping/ping_sock.c)
and local SDK headers. Queue, task, sample and sensor sizes were measured with
target DWARF, symbols, and compiler-generated size probes, not host layouts.

## CPU and stack bounds

The Wi-Fi callback validates metadata and gates captures at 50 ms **before**
copying/enqueueing: at most 20 frames/second, even during image/OTA traffic.
Full queues drop immediately. It never classifies, logs, or allocates. The SDK
still performs CSI work on received packets; this gate does not remove that
radio-driver cost.

The existing Core 0 web task drains at most four frames per iteration, with
51 carriers/frame and fixed linear arithmetic. There are no FFTs, sorting,
growing windows, or model allocations. Frames older than 250 ms expire, so
slow HTTP/SD work cannot accumulate a replay backlog. Motor code, main's static
Wi-Fi TX/RX buffer budget, AsyncTCP core affinity and its 8 KiB stack are intact.
The SDK's 10 Hz ping task is priority 1, without explicit core affinity; the
classifier/automation remain on the existing web task. HT20 can reduce Wi-Fi
throughput and still requires hardware load testing.

Ping uses single-probe SDK batches with an application 100 ms minimum spacing.
The next batch waits for the previous completion callback; task/socket/packet
storage is reused. This avoids the repeating SDK loop's catch-up sends after
timeouts or scheduling delays. Pausing allows at most one already-issued probe
to finish. A handle-specific atomic completion handshake prevents an old
session's delayed callback from completing a newer session.

Replaying production commands with `-fstack-usage` gives these own-frame sizes:

| Function | Bytes |
| --- | ---: |
| Wi-Fi CSI callback | 192 |
| Web task entry | 144 |
| Presence loop | 224 |
| Sample processing | 272 |
| Detector addPowers | 304 |
| Normalization | 48 |
| Status snapshot | 64 |
| Light automation update | 32 |

The listed processing call chain sums to 992 bytes including the web entry,
within its unchanged 8,192-byte stack. SDK callees, interrupt frames, and other
web paths are excluded: this is not whole-task worst-case stack usage. The
callback's frame belongs to the Wi-Fi task, not the web task.

## Regression checks

`test/test_presence_budget.cpp` passes with ASan/UBSan: heap hysteresis,
fragmentation refusal, six million simulated callback admissions (exactly 20
accepted/second), non-bursting ping pacing after a long gap, clock wrap, and
one million frames across 1,000 calibrations
without a C++ heap allocation. Its host duration is not an ESP32 CPU benchmark.
Existing response-buffer/transport tests also pass with ASan/UBSan, including
allocation refusal, cancellation, partial/zero writes, and exact delivery of
the actual Settings HTML. All seven browser-script test files pass.
All eleven ESP32 build environments pass, including production/OTA, no-motor,
UART scan, motion-test, theta/rho commissioning, and rho startup-trial variants.
The native synthetic suite and all 24 repository pattern simulations pass.

```sh
g++ -std=c++17 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined \
  -Ilib/PresenceDetection/src -Ilib/PresenceAutomation/src \
  test/test_presence_budget.cpp lib/PresenceDetection/src/PresenceDetector.cpp \
  lib/PresenceAutomation/src/PresenceAutomation.cpp -o /tmp/test_presence_budget
/tmp/test_presence_budget
```

## Remaining hardware check

The source, host tests, and target builds establish a small, bounded application
cost. They cannot certify that the combined radio/socket/web/motion workload
never exhausts or fragments heap on the installed ESP32.

Before deployment, compare idle sensing/calibration and concurrent image,
status, and upload load with main on an appropriate bench. Record minimum free
heap, minimum largest block, W/M/F and AsyncTCP stack high-water marks, request
latency, step lateness/underruns, and repeated AP reconnect recovery.
`GET /api/presence` adds `memoryLimited` and `maxProcessingUs`; the latter is
the lifetime maximum wall time inside the sensing-loop lock, including SDK
calls/preemption but excluding mutex-acquisition wait. Verify suspension and
recovery rather than treating static RAM percentages as a runtime guarantee.
