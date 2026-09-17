# Sisyphus Table Controller

ESP32 firmware for a Sisyphus-style polar-coordinate sand table. It drives Theta/Rho steppers, streams pattern files from SD, serves a Web UI/API, supports OTA updates, and provides motion tuning and diagnostics.

## Features
- Dual-core task split: real-time motion on Core 1, UI/network/file streaming on Core 0.
- S-curve motion planning with synchronized Theta/Rho timing and lookahead step generation.
- Web UI with live position visualization, playlist control, and tuning controls.
- SD card pattern storage, uploads, and optional PNG previews.
- OTA firmware updates and runtime telemetry.
- Experimental Wi-Fi CSI motion/occupancy sensing with empty-room calibration
  and an optional presence-triggered light fade.

## Architecture
At a high level the firmware is organized around three FreeRTOS tasks pinned across the two ESP32 cores. `src/main.cpp` creates a MotorTask on Core 1 for deterministic step generation, and a WebTask on Core 0 for the Web UI/API, WiFi, and OTA handling. `lib/PolarControl` owns the motion planner, driver configuration, and the inter-task queues. It also spins up a FileReadTask on Core 0 to stream `.thr` pattern points from SD/LittleFS into a coordinate queue. The MotorTask drains that queue and drives the stepper outputs, while the WebTask sends commands (start/stop/speed/tuning) and exposes telemetry back to the UI via SSE and JSON APIs.

![Architecture diagram](docs/images/architecture-tasks-dark.svg)

## Hardware
- ESP32 dev board
- 2x (or 3x) TMC2209 stepper drivers (UART)
- 2x stepper motors (Theta + Rho)
- SD card module
- 12V/24V PSU sized for motors and LEDs

## Quick Start
1. Open the project in PlatformIO.
2. Adjust WiFi credentials in `lib/Config/src/Config.h` if desired.
3. Build and flash:
   ```bash
   pio run -t upload
   ```
4. Upload filesystem assets if needed:
   ```bash
   pio run -t uploadfs
   ```
5. Connect to the device IP and open the Web UI.
6. With the current main-only hardware, place RHO at its physical center stop
   and use **Set Home** on the manual page. Pattern motion remains locked until
   the operator establishes the origin.

Automatic homing stays disabled (`Config::kAutoHomeOnBoot = false`) until the
assembled mechanism passes unknown-origin and cold-start qualification.

## Configuration
Key settings in `lib/Config/src/Config.h`:
- WiFi AP fallback credentials
- Static IP defaults (`100.76.149.200`)
- OTA hostname/password
- Task core affinity, stack sizes, and telemetry intervals

Pin defaults live in `lib/Config/src/Config.h`:
- LED PWM: 4 (D4 / GPIO4), internal pull-down enabled during LED initialization
- Rho Step: 33
- Rho Dir: 25
- Theta Step: 32
- Theta Dir: 22
- UART RX: 27
- UART TX: 26
- Driver UART addresses: 0/1/2

## Web UI and API
![Web UI screenshot](docs/images/webserver-screenshot.png)

Core endpoints (see `lib/WebServer/src/SisyphusWebServer.cpp`):
- `GET /` UI
- `GET /settings` presence behavior and machine commissioning settings
- `GET /api/status` current state and telemetry
- `GET /api/stream` SSE position stream
- `POST /api/pattern/start` start a pattern
- `POST /api/pattern/stop|pause|resume` control playback
- `GET /api/files` list files
- `POST /api/files/upload` upload `.thr` and optional `.png`
- `POST /api/files/delete` delete a pattern
- `GET|POST /api/led/brightness` LED control
- `GET|POST /api/speed` speed control
- `GET /api/tuning/*` driver and motion tuning
- `GET /api/presence` CSI presence telemetry
- `POST /api/presence/calibrate` start empty-room calibration
- `GET|POST /api/settings/presence` configure the movement response
- `POST /api/home` start sensorless homing
- `POST /api/home/confirm` accept or reject the observed home position

## Pattern Format (.thr)
Text file of polar coordinates in radians and normalized radius:
```
# comments with # or //
0.0 0.5
0.1 0.5
...
6.28 0.5
```
- `theta`: radians
- `rho`: normalized 0.0 to 1.0 (scaled by max radius)
- Separators: space, comma, or tab

File parsing ignores empty/comment lines. Lines longer than 127 characters are skipped and reported to the error log.

## File Storage Layout
- Required: `/patterns/name/name.thr` and `/patterns/name/name.png`

The Web UI accepts `.thr` plus optional `.png` uploads with the same base name. This png should be an image of the path produced by the thr with a white line and transparent backgroud. This will be overlayed on the webpage over the position viewer.

## Motion Planning
- One jerk-limited progress profile drives both axes along each polar THR segment.
  Angles remain unwrapped, with double precision and exact nominal gearing.
- Corner smoothing is bounded to 0.10 mm by default; set **Corner Smoothing** to
  zero for the exact piecewise polar path, stopping where its direction changes.
- Ball speed and planar acceleration are limited alongside the motor limits.
  Defaults are 30 mm/s and 100 mm/s²; the speed slider scales velocity limits.
- Lookahead uses integer microsecond clocks and flushes each exact motor target.
  Pause, stop, and speed changes brake along the source curve; resume continues
  the unfinished portion before taking the next waypoint.
- Files are validated completely before coordinates enter the motion queue.
  Malformed coordinates, out-of-range radius, overlong lines (over 127 bytes),
  embedded NULs, empty files, and motor-step overflow reject the file with a line
  number. Blank lines and `#` / `//` comments are accepted.
- Moving from the current position to the first THR coordinate is an explicit
  approach and can draw a connecting line. The existing playback controller resets
  the logical theta origin at each new pattern; within that frame, file angles
  stay unwrapped and the planner preserves every requested turn.

See [motion accuracy implementation and validation](docs/MOTION_ACCURACY_IMPLEMENTATION.md)
for measured changes, timing tradeoffs, reproduction commands, and physical checks.

The [selected sound-tuned settings](docs/SOUND_TUNED_SETTINGS.md) summarize
both axes' current profiles, including the accepted RHO 350 mA run / 200 mA
hold setting, driver parameters, persistence, and qualification limits.

Loaded theta commissioning, acoustic acceptance, known resonances, selected
defaults, and the repeatable retuning procedure are documented in the
[theta acoustic tuning playbook](docs/THETA_ACOUSTIC_TUNING.md).

RHO acoustic commissioning uses an outward-only temporary origin and is
documented in the [rho acoustic tuning playbook](docs/RHO_ACOUSTIC_TUNING.md).

Wi-Fi sensing setup, behavior, and limitations are documented in the
[presence sensing guide](docs/PRESENCE_SENSING.md). The measured ESP-IDF 5.4
migration surface and recommendation are in the
[upgrade assessment](docs/ESP_IDF_5_4_UPGRADE.md).

## Sensorless Homing

The current assembly has a main RHO motor on UART address 0 and an empty
counterweight motor socket on address 1. Firmware keeps the empty driver's
bridge off. Main RHO uses STEP/DIR for a pulse-capped 1 mm inward entry,
6 mm outward runway, and 12 mm/s inward StallGuard approaches with up to
6 mm backoff between contacts. UART
configures the drivers and supplies `SG_RESULT`; it does not command homing
velocity. Homing requires three consecutive contact coordinates spanning no
more than 0.4 mm, with a maximum of twelve approaches and a 90-second deadline.

Production enables automatic boot homing after starting the website; use
**Abort homing** to stop and invalidate zero. The service image still boots
without motion and requires physical review of test results. Production uses
SG data alone; neither the microphone nor camera is a runtime dependency.

The startup qualification build passed from known starts at 0, 10, 25, 100
and 400 mm. Production warm boots passed at zero and 10 mm; the user also
confirmed a successful power-cold boot from zero. A nominal 425 mm start reached
camera home but failed the independent position veto; retain that rejection.
SG consensus cannot distinguish every repeatable obstruction from home.
After the first candidate, retries permit at most 2 mm new inward command
progress per pass and 4 mm total, separate from the capped entry probe.
These command limits do not measure physical penetration through a stop.
See the [RHO homing playbook](docs/RHO_HOMING_TUNING.md) before powered tests.
The counterweight motor needs its own qualification when reconnected.

## Logging and Diagnostics
- Serial logs include queue depth, underruns, timing stats, and state changes.
- Error log is stored in memory and exposed via `GET /api/errors`.

## Tests
Native motion planner tests:
```bash
pio run -e native
./run_all_tests.sh
```

The pattern suite actually loads and simulates each `.thr` file, verifies the
completed segment count and final step position, and rejects malformed inputs.
There is intentionally no CI requirement for this hobby project; run the native
suite and the ESP32 build locally before flashing.
