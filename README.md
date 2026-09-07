# Sisyphus Table Controller

ESP32 firmware for a Sisyphus-style polar-coordinate sand table. It drives Theta/Rho steppers, streams pattern files from SD, serves a Web UI/API, supports OTA updates, and provides motion tuning and diagnostics.

## Features
- Dual-core task split: real-time motion on Core 1, UI/network/file streaming on Core 0.
- S-curve motion planning with synchronized Theta/Rho timing and lookahead step generation.
- Web UI with live position visualization, playlist control, and tuning controls.
- SD card pattern storage, uploads, and optional PNG previews.
- OTA firmware updates and runtime telemetry.

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
6. Run homing from the dashboard and visually confirm that the carriage reached
   the physical center stop. Pattern motion remains locked until confirmation.

Automatic homing is disabled by default (`Config::kAutoHomeOnBoot = false`) so
firmware can be developed with the mechanism disconnected.

## Configuration
Key settings in `lib/Config/src/Config.h`:
- WiFi AP fallback credentials
- Static IP defaults (`100.76.149.200`)
- OTA hostname/password
- Task core affinity, stack sizes, and telemetry intervals

Pin defaults live in `lib/Config/src/Config.h`:
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
- S-curve profiles per axis with synchronized segment durations.
- Lookahead step generation into a queue to avoid underruns.
- Pause and stop use controlled deceleration; pause preserves planner targets
  that have not yet been generated.

Loaded theta commissioning, acoustic acceptance, known resonances, selected
defaults, and the repeatable retuning procedure are documented in the
[theta acoustic tuning playbook](docs/THETA_ACOUSTIC_TUNING.md).

Paired-rho acoustic commissioning uses an outward-only temporary origin and is
documented in the [rho acoustic tuning playbook](docs/RHO_ACOUSTIC_TUNING.md).

## Sensorless Homing

The two rho motors home sequentially over their normal shared STEP/DIR signal.
For each pass, the other driver's power stage is disabled, the active motor
makes a bounded 4 mm outward runway move, and then approaches the center stop
at constant speed while StallGuard is sampled over its addressed UART. The
motor backs out 4 mm and repeats the inward approach slowly; homing succeeds
only when StallGuard recovers during the backoff and the second trigger occurs
within the expected return window. Both drivers remain disabled after any
failure.

Initial samples are ignored, low readings must persist for a configured number
of samples, and every move has a step and time bound. Because disabled TMC2209
indexers still see the shared STEP signal, firmware restores each inactive
driver's recorded electrical phase while its power stage remains off before it
can be re-enabled. A visually rejected result also leaves both rho power stages
disabled. The automatic result is still not treated as proof: the dashboard
requires visual confirmation before entering `IDLE`.

Sequential homing assumes the de-energized motor is not mechanically
back-driven by the active motor. That must be confirmed during the first guarded
powered run; this method is not suitable for a mechanism that moves both motor
shafts when only one is driven.

The Tuning page exposes the three values useful for rejecting stiff-path false
positives: trigger percentage, consecutive low samples, and ignored initial
travel. See [hardware validation](docs/HARDWARE_VALIDATION.md) before connecting
the mechanism.

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
