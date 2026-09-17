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
4. Open **Settings** and select **Calibrate empty room**.
5. Keep the room and table still until progress reaches 100%, normally about
   16-20 seconds at the built-in 10 Hz sample rate.
6. Walk through the radio path between the table and access point and watch the
   activity score, motion state, and one-minute occupied hold.

Calibration is intentionally per boot in this first implementation. Moving the
table or access point changes the radio path and requires another calibration.

## API

- `GET /api/presence` returns CSI availability, sample freshness, calibration,
  suppression, motion/occupied state, activity score, threshold, RSSI, and
  packet counters.
- `POST /api/presence/calibrate` starts a new empty-room calibration. It returns
  `409` while the mechanism is moving, during the five-second settling time,
  or when fresh gateway CSI samples are unavailable.
- `GET|POST /api/settings/presence` reads or changes the movement response.
  The `action` field accepts `none` or `fade_light_on` and is retained in NVS.
- `GET /api/status` includes the same data in its `presence` object.

The normalized `score` is relative to the learned threshold. A sustained score
at or above `1.0` enters motion after three accepted samples. Motion clears with
hysteresis, while `occupied` remains true for 60 seconds after the last strong
motion sample.

When **Fade light on** is selected, a new calibrated motion event fades the
light from its current level to 100% over two seconds. Sustained motion does not
continually retrigger the fade, and a manual brightness change cancels a fade
already in progress. The saved response survives reboot, but calibration does
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
the Wi-Fi callback cannot delay the networking stack. Only the AP BSSID is
accepted, and LLTF input is limited to 20 samples per second.

If this cannot reliably distinguish walk-up activity in the installed room,
the next step should be a second fixed ESP32 placed across the desired sensing
zone or a dedicated mmWave sensor. Do not tune it into a motor safety interlock.
