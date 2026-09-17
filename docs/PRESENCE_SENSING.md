# Wi-Fi CSI presence sensing

The table uses the ESP32 radio's channel-state information (CSI) as an
experimental motion and occupancy sensor. It requires no additional GPIO or
sensor module. The ESP32 sends a low-rate ping to its configured gateway and
measures how the received Wi-Fi multipath shape changes.

This feature never starts, pauses, or stops the motors and is not a safety
system. Its only optional automation is fading the table light to full
brightness after newly detected movement.

## First use

1. Put the table in its normal location and leave it connected to its normal
   Wi-Fi access point.
2. Stop all table motion. Wait until the dashboard no longer says presence
   sensing is suspended.
3. Clear people and moving objects from the room.
4. From outside the room, open **Settings** and select **Calibrate empty room**.
   Sampling starts immediately; there is no leave-room countdown.
5. Keep the room and table still until progress reaches 100%, normally about
   16-20 seconds at the built-in 10 Hz sample rate.
6. Walk through the radio path between the table and access point and watch the
   activity score, motion state, and one-minute occupied hold.

Calibration is intentionally per boot in this first implementation. Moving the
table or access point changes the radio path and requires another calibration.
Disconnecting or changing the AP, channel, or gateway invalidates calibration.
Motor movement or a gap of more than three seconds in usable samples aborts an
unfinished calibration; start it again with the room empty.

## API

- `GET /api/presence` returns CSI availability, sample freshness, calibration,
  suppression, motion/occupied state, activity score, threshold, RSSI, and
  packet counters, plus `memoryLimited` and the lifetime maximum sensing-loop
  wall time `maxProcessingUs` (excluding mutex-acquisition wait).
- `POST /api/presence/calibrate` starts a new empty-room calibration. It returns
  `409` while the mechanism is moving, during the five-second settling time,
  when calibration is already running, or when fresh AP CSI samples are unavailable.
- `GET|POST /api/settings/presence` reads or changes the movement response.
  The `action` field accepts `none` or `fade_light_on` and is retained in NVS.
- `GET /api/status` includes the same data in its `presence` object.

The normalized `score` is relative to the learned threshold. A sustained score
at or above `1.0` enters motion after three accepted samples. Motion clears with
hysteresis, while `occupied` remains true for 60 seconds after motion ends,
provided samples remain fresh and sensing is not suppressed. This is a recent
movement indicator, not proof that a stationary person is present.

The reference channel shape adapts by 2% per accepted sample, including during
motion, so a static change cannot hold the motion state forever. After a sample
gap or motor settling, the first usable frame seeds a new reference while the
learned noise threshold is retained. That frame cannot detect a walk-up by
itself. These are experimental heuristics, not Espressif's packaged algorithm.

When **Fade light on** is selected, a new calibrated motion event fades the
light from its current level to 100% over two seconds. Sustained motion does not
continually retrigger the fade, and a manual brightness change cancels a fade
already in progress and consumes the current motion event. Selecting **Do
nothing**, losing valid samples, or starting calibration also cancels a fade.
New events during a fade do not extend its two-second duration. The light never
turns off automatically. The saved response survives reboot, but calibration does
not. **Fade light on** is the default response; select **Do nothing** to disable
the automation while keeping presence telemetry active.

## Motion isolation and limitations

CSI responds to movement anywhere that materially changes the multipath between
the table and access point. It does not provide a precise distance or identify a
person. A motionless person can time out. Fans, pets, doors, and people outside
the desired table area can trigger it.

The moving steel ball and carriage are especially strong local reflectors. CSI
processing is therefore suspended during running, stopping, clearing,
preparation, and homing, then held off for five seconds. Results during that
period are unknown, not evidence that the room is empty.

The callback only copies the transient CSI payload into a fixed FreeRTOS queue.
Normalization and classification run later on the existing Core 0 web task so
the Wi-Fi callback does not run the detector. Only the current AP BSSID and
channel are accepted, and LLTF input is limited to 20 samples per second.
The station is restricted to HT20 (20 MHz), which can reduce Wi-Fi throughput.
Frames with errors, an incompatible layout, zero active-carrier power, or more
than 250 ms in the queue are discarded. The 51 retained carriers exclude DC,
guard carriers, and the possibly invalid first four bytes. Mixed HT20/HT40
layouts are never compared. If packet counters grow without fresh samples,
check the AP's 20 MHz compatibility as well as gateway ping responses.

The carrier layout follows the
[ESP-IDF 4.4.7 CSI documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-guides/wifi.html#wi-fi-channel-state-information).

If this cannot reliably distinguish walk-up activity in the installed room,
the next step should be a second fixed ESP32 placed across the desired sensing
zone or a dedicated mmWave sensor. Do not tune it into a motor safety interlock.

## Validation

Capture uses a four-frame static queue, with a 20 Hz rate limit before copying
in the Wi-Fi callback. Low heap or severe fragmentation suspends sensing and
ping traffic; Settings reports this explicitly. Memory must recover before
calibration or light automation can resume. See the
[memory/performance checks](PRESENCE_PERFORMANCE.md) for measured budgets,
thresholds, and remaining hardware tests.

Host regression tests cover detector calibration, interrupted calibration,
sampling gaps, invalid inputs, clock wrap, static-channel recovery, occupancy
expiry, and fade/manual-override behavior. Settings-page tests exercise the
embedded JavaScript with mocked DOM and network responses:

```sh
pio run -e native
.pio/build/native/program
node --test test/test_presence_ui.cjs
```

These tests do not validate radio sensitivity, real AP metadata, ESP32 task
timing, or false-positive rates. Before deploying, test empty-room drift,
walk-ups, stationary occupants, fans/pets, motor start/stop, router reconnects,
calibration interruption, manual light overrides, and OTA/web traffic on the
installed hardware. No hardware has been flashed as part of this review.
