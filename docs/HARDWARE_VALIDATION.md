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
2. For the current main-only assembly, use the RHO service image and confirm
   `homing.companionMotorEnabled=false`. Read both driver dumps: main RHO must
   answer at address 0, and the empty address-1 bridge must report `TOFF=0`.
   The service image locks out theta and pattern motion.
3. Confirm main RHO moves outward for positive RHO jogs without binding. Stop
   if it slips or stalls. Manually establish physical zero, then select **Set
   current position as home** before any known-origin trial.
4. Run the bounded trial in the [RHO homing playbook](RHO_HOMING_TUNING.md).
   For the startup-entry build, expect a capped 1 mm inward probe, a 6 mm
   outward runway, and 12 mm/s inward approaches with up to 6 mm backoff.
   Require three consecutive contact coordinates within a 0.4 mm total span.
   Ordinary service builds retain the historical 8 mm initial runway.
   Keep hands clear and a
   physical power cutoff within reach.
5. Compare the final mechanism position with the confirmed zero reference.
   Reject any early stop, missed contact, driver fault, or mismatch. A rejected
   result disables both RHO bridges; re-establish zero before another trial.
6. Preserve the SG trace and both driver dumps for each attempt. The STEP
   ledger and `MSCNT` show commanded or electrical motion, not shaft position.
   Do not adjust the trigger threshold from one run without replaying the
   change against recorded false-trigger and true-stop traces.
7. Requalify the counterweight motor and the paired phase-hold path before
   connecting its load or enabling paired homing. Main-only results do not
   establish paired behavior.

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

Main-only production enables boot homing. Warm tests cover known starts
through 400 mm and production boots at zero and 10 mm. The user confirmed
the new entry's power-cold boot at zero; firmware reported a power-on reset,
three contacts spanning 0.300 mm, and 311/311 valid UART reads. See the
current evidence and limitations in the homing playbook.
Recheck after assembly, load, alignment, driver or homing-profile changes.
Three agreeing SG contacts do not prove that a fixed constriction is absent.
