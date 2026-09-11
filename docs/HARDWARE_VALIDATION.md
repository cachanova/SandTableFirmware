# Hardware validation

The native tests and ESP32 build cannot prove motor direction, available torque,
StallGuard behavior, or mechanical clearances. Validate those deliberately when
the mechanism is connected.

## Before applying motor power

1. Confirm the configured wiring: theta STEP/DIR are GPIO32/22, rho STEP/DIR
   are GPIO33/25, and the shared TMC2209 UART is RX GPIO27 plus TX GPIO26.
   GPIO1/GPIO3 belong to the USB debug UART and must not connect to PDN_UART.
   Use the board-required one-wire UART coupling resistor (typically about 1 kΩ).
2. Confirm the three drivers have unique UART address straps: theta 2, primary
   rho 0, and companion rho 1. All VIO connections must be 3.3 V and share a
   ground with the ESP32.
3. Make sure the carriage can move through the full rho path by hand and note
   the angular regions that are mechanically stiff.
4. Keep a physical power cutoff within reach. Sensorless homing is not an
   emergency-stop mechanism.
5. Start with conservative run current, velocity, acceleration, and jerk.

## Electronics-only diagnostic build

When isolating wiring or power problems, flash the `esp32dev_no_motor`
environment. It initializes neither UART1 nor any TMC2209 driver, so it cannot
enable or command a motor, while Wi-Fi, the Web UI, LittleFS tuning, and the
read-only diagnostics remain available:

```sh
pio run -e esp32dev_no_motor -t upload --upload-port /dev/ttyUSB0
```

If serial corruption or resets persist with that image, the cause is external
to motor-driver firmware access. Disconnect PDN_UART first, then add one driver
at a time while checking VIO, common ground, supply stability, and reset strap
pins GPIO0/2/5/12/15. Return to the normal `esp32dev` build before mechanical
commissioning.

## Electronics-only motion timing

With the drivers connected but **no motors attached**, the bench-motion image
can exercise the real ESP32 timer, STEP/DIR generation, motion arbitration, and
Web UI without running sensorless homing:

```sh
pio run -e esp32dev_motion_test -t upload --upload-port /dev/ttyUSB0
```

This image deliberately assumes rho=0 and theta=0 at boot. Confirm that
`GET /api/motion/telemetry` reports `"benchMotionTest":true` and `IDLE` before
testing. The same endpoint reports actual logical position and velocity, step
queue depth, underruns, completed segments, and timer state.

Do not use this image with motors connected: its assumed origin is not a
physical reference. Stop all motion when the test is done, then restore the
normal homing-locked firmware:

```sh
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0
```

## First powered checks

1. Boot with `kAutoHomeOnBoot = false`. The dashboard should show
   `INITIALIZED`, and pattern start requests should be rejected.
2. Use short motor tests to verify theta and rho direction with the mechanism
   able to move safely. Stop immediately if either axis binds.
3. Start homing from the dashboard and watch both sequential motor passes.
   For each motor, verify a short outward runway, a constant-speed inward
   approach, a 4 mm verification backoff, and a slow inward return. The other
   rho motor must remain mechanically stationary while its driver holds its
   saved phase at 256 microsteps using `VACTUAL=+/-1`. If that shaft moves,
   drifts, or jumps when STEP/DIR control is restored, cut power and reject the
   sequential homing method for this hardware.
4. If motion stops in a stiff section, choose **No, It Stopped Early**. On the
   Tuning page, first lower the trigger percentage, then increase consecutive
   samples or ignored initial travel. Change one value at a time.
5. If the hard stop is reached but never detected, raise the trigger percentage
   or reduce consecutive samples. Do not compensate by raising motor current
   until the mechanical path and driver temperature have been checked.
6. Confirm home only when both mechanisms are visibly at their physical center
   stops. Record both precision-pass times and StallGuard trigger/baseline pairs
   shown on the dashboard; large changes on later runs are a useful warning
   sign.
7. On the first powered run, be ready to cut power when either driver's phase
   is restored or the pair is re-enabled. A phase-restore failure must leave
   both rho power stages disabled and report failure 9; do not proceed to
   pattern motion if either motor jumps on re-enable.

## Motion checks

1. Run a very short, low-speed pattern near the center.
2. Exercise pause and resume. The table should decelerate, display `PAUSED`, and
   resume without skipping ahead in the pattern.
3. Exercise stop at low and then normal speed. `STOPPING` should remain visible
   until motion has actually ceased.
4. Run one full representative pattern while watching queue underruns, driver
   temperature, missed steps, belt/cable behavior, and unexpected resonances.
5. Only after repeatable homing and clean representative runs should higher
   tuning limits be considered.

Do not enable automatic boot homing until multiple cold-start and warm-start
runs have reached the real hard stop reliably across the stiffest theta regions.
