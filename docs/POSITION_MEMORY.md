# Position memory

The controller remembers where the ball stopped so a boot can skip sensorless
homing. Homing still runs whenever that memory cannot be trusted.

## What is stored

One record in internal NVS (`Preferences` namespace `position`, key `last`):

| Field   | Meaning                                                        |
|---------|----------------------------------------------------------------|
| schema  | Layout version. A record from another version is ignored.      |
| valid   | `true` only while the table is settled at a trusted position.  |
| theta   | Planner angle in radians.                                      |
| rho     | Planner radius in millimetres.                                 |

`lib/PolarControl/src/PositionMemory.hpp` holds the codec; the NVS adapter is
in `PositionMemory.cpp`. NVS is used instead of the SD card or LittleFS because
a blob write is atomic under power loss, the partition is wear levelled, and the
record is 20 bytes. A torn write is impossible: the next boot sees either the
old record or the new one.

## When it is written

Every write happens under the controller mutex.

- `valid=false`, before the first step of any motion: pattern start, file
  load, clearing, direct `moveTo`, manual jogs, `resume`, and the start of
  homing.
- `valid=true` with the current position whenever the table settles with a
  trusted position: motion complete, controlled stop, pause, homing confirmed,
  "set current position as home", and theta reset.

Emergency stop writes nothing. If motion was under way the record is already
invalid. If the table was idle, as during an OTA update, the record stays valid
and the reboot after the update skips homing too.

Bench and commissioning images (`SISYPHUS_BENCH_MOTION_TEST`,
`SISYPHUS_THETA_COMMISSIONING`, `SISYPHUS_RHO_COMMISSIONING`) never write a
record and discard any they find at boot, so a service session cannot hand a
stale position to the next production firmware.

## What happens at boot

`PolarControl::restoreSavedPosition()` runs where automatic homing used to
start, after the web server is up:

1. No record, a corrupt record, or `valid=false`: log why and fall through to
   the existing behaviour (automatic homing, or waiting for the UI in
   `esp32dev_full_manual`).
2. A valid record but a configured motor driver offline: same fallback.
3. Otherwise set the planner to the saved position, mark both rho motors
   referenced, enter `IDLE`, and log `Restored saved position`. Theta is
   reduced to one turn first so the step counters stay bounded over many boots;
   patterns already start on the ball's current lap, so no turn information is
   lost.

## Accuracy

A power cycle still moves the rotor slightly. Hold current stops, the rotor
relaxes to the nearest detent, and on the next enable the driver energises at
its default microstep phase, so each axis can shift by up to two full steps per
cycle. With 50 full steps per millimetre on rho that is at most 0.04 mm. With
the 60:16 theta gearing it is at most about 1 degree, and theta has no
mechanical limit or absolute reference. Neither accumulates in a way the sand
shows, so the extra complexity of restoring the driver phase was not taken.

The firmware cannot detect the arm being moved by hand while the power is off.
Use **Re-home** on the dashboard when that happens.

## Tests

```sh
cxxflags="-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined"
g++ $cxxflags -Ilib/PolarControl/src test/test_position_memory.cpp \
  -o /tmp/test_position_memory
/tmp/test_position_memory
python3 test/test_rho_jog.py
```

The first covers the record codec, rejection of corrupt, foreign, and
out-of-range records, theta normalisation, and the rho tracker reference. The
jog harness checks that a jog invalidates the record before motion and that a
jog completing in `IDLE` saves the settled position while one completing in
`INITIALIZED` does not.
