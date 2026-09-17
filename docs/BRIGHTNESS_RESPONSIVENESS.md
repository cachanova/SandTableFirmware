# Brightness responsiveness campaign

## Scope and baseline

The reported symptom was a brightness change followed by darkness and a long
delay before further changes took effect. Reset restored the light. The code
has no automatic dim/off action; presence only fades upward and was uncalibrated
and suppressed during motion. API brightness was a cached command, not a
measurement of the GPIO or LED circuit.

Baseline firmware: `1023388`, ESP32 at `100.76.149.200`, 2026-09-17. While
`Spiral8.thr` ran, 30 repeated commands at the existing 100% target, with concurrent
status and file-list requests, all succeeded. POST latency was median 38.590 ms,
p95 1084.319 ms, maximum 1094.244 ms. Uptime advanced from 131 to 147 seconds;
there was no reset during the probe. Slow requests also affected file-list and
brightness reads. This reproduces transport/request latency, not the physical
blackout or its cause. Raw baseline: `/tmp/brightness-before.json`.

## Changes

- The slider sends while being dragged, with at most one outstanding request,
  one replaceable pending value, and a maximum send rate of 10 Hz. Intermediate
  edits are discarded; the final value is retained. Pointer capture keeps drag
  state correct when release occurs outside the slider.
- A 2.5-second deadline covers headers and JSON acknowledgement. Errors and
  uncertain outcomes remain visible beside the control. Uncertain commands and
  their pending backlog are not automatically replayed after reconnection.
- Status requests carry the brightness revision at their start. Replies begun
  before an edit or before its acknowledgement cannot overwrite the newer
  selection. Duplicate input/change events do not send duplicate commands.
- A separate static LED mutex serializes manual writes, presence fades, and
  PWM diagnostics. Motion commands, presence sampling, flash settings writes,
  response allocation, and logging run outside it. A manual command waits at
  most 10 ms for this mutex, then returns an explicit busy response; an automatic
  fade skips a busy tick. Other AsyncTCP callbacks and Wi-Fi can still delay
  request dispatch; this is not an end-to-end real-time guarantee.
- PWM writes check driver return values. Applied brightness and the manual
  target change only on successful writes. The 8-bit full-on mapping remains
  255 -> 256, and PWM remains 5 kHz on GPIO4 with its internal pull-down.

`GET /api/led/brightness` retains `brightness` and `targetBrightness` and adds
`pin`, `pwmReady`, `pwmDuty` (raw 0–256 register value), `pwmFrequencyHz`,
`writeCount`, `writeErrors`, `lastWriteUs`, `maxWriteUs`, `lastRequestUs`,
`maxRequestUs`, `lastLockWaitUs`, and `maxLockWaitUs`. Request timing starts at
handler entry and ends after the PWM call; it excludes browser queuing, network
delivery, request parsing, and acknowledgement transmission. Register readback
does not measure GPIO voltage, gate voltage, or LED current.

## Reproducing the measurement

```sh
python scripts/brightness_probe.py --samples 30 --load --output /tmp/brightness-after.json
```

Run while no one else is using brightness. The probe reapplies the current
percentage and does not start/stop motion or edit storage. It stops on observed
target changes, errors, readback mismatch, or reboot. An external edit can race
the read-before-write check, so do not share the slider during the probe.
Without `--load`, only LED requests and initial/final status are fetched.

## Local validation

- Production ESP32 build passes.
- All browser test files pass, including eight new cases for coalescing,
  throttling, body-inclusive timeout, explicit retry, driver rejection, stale
  polls, external edits, and live slider input.
- The real LED and presence automation classes pass ASan/UBSan host tests on
  both Arduino 2 and 3 API branches, including full-on register mapping,
  rejected writes preserving target/output state, and recovery after failure.
- A real Chromium drag across five intermediate positions against a delayed
  mock server sent only 19% then 100%, never exceeded one in-flight command,
  acknowledged 100%, and cleared drag state when released outside the slider.

## Hardware validation and remaining delay

The user authorized stopping the current drawing, OTA deployment, restarting
`Spiral8.thr`, and merging after validation. Firmware `3543fe4` uploaded
successfully and completed automatic homing. Brightness was restored to 100%
and speed to 10; Spiral8 was restarted without a clearing pass.

| Check | Result |
| --- | --- |
| Duty sweep, 0/1/16/33/50/99/100/0/100% | Every target and PWM register matched; no write errors |
| 30-second stationary load, images/pages/two SSE clients | Passed, no reset or new errors; both streams received 30 events |
| Byte-addressable heap during stationary load | Minimum 23,104 bytes; 23 expected busy responses preserved admission control |
| 30 LED commands during Spiral8 + concurrent status/files | All succeeded; p50 41.288 ms, p95 72.890 ms, max 1098.437 ms |
| Slowest command in that run | Handler 263 us, PWM call 125 us, LED-lock wait 11 us |
| Live Chromium page, rapid drag and release outside slider | Four POSTs, at most one outstanding, final 100% acknowledged; no JS exceptions |
| Motion after testing | Zero planner underruns, zero recorded step-timing outliers |
| Final LED diagnostics | GPIO4, 5 kHz, raw duty 256, zero write errors; maximum handler 877 us, lock wait 18 us |

The 30-command comparison has a better p95 but retains a one-second outlier.
The baseline ran at speed 5 and the post-flash run at the then-current speed 10;
these short samples under different motion loads do not establish a controlled
speedup or prove that all end-to-end latency is fixed.
A separate 30-command transport probe recorded two requests with TCP
retransmissions. The slowest spent 1043.99 ms in TCP connection establishment,
then 22.92 ms waiting for response headers (1067.22 ms total). Thus at least one
remaining second-long delay occurs before the brightness handler is reachable,
not while applying PWM. This does not identify which network hop dropped the
packet, nor establish the cause of the reported physical blackout.

Existing browser tabs must reload to receive the new request handling. The
running table was left drawing Spiral8 at brightness 100%. It was restarted at
speed 10; the final snapshot later showed speed 7, which was left unchanged.

Machine-readable summary: [results.json](brightness-responsiveness/results.json).
Full temporary artifacts, including before/after requests, the duty sweep,
stress report, browser result, transport counters, final telemetry, and the
matching firmware binary/ELF, are in `/tmp/brightness-campaign-artifacts/`.
Firmware SHA-256:
`31711bfb80c35a4f3e31362c7326d91bca50d1f13fc7b2d45d6711d5d0e839f6`.
