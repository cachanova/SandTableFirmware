# RHO sensorless-homing tuning playbook

## Current checkpoint (2026-09-13)

The replacement drivers at RHO address 0 and RHO-CW address 1 now answer UART
reads. The operator assembled the table, placed the RHO mechanisms at physical
zero, and allowed bounded homing tests without camera verification. The
experimental method still needs measured trials and physical qualification.
Keep automatic boot homing off and treat the current detector values as a
starting point.

The regular image accepted Set as Home at rho 0 after we cleared a stale web
motion flag. Installing the RHO commissioning image rebooted the ESP32 and
cleared that logical zero. The operator then left the site and asked us to use
the prior zero confirmation, microphone, and command bounds. Post-upload
telemetry showed no STEP motion since boot. We accepted that zero as a
provisional trial reference, then ran one guarded test. It failed on the CW
outward runway; do not use the resulting logical zero for another trial.

What is established:

- `SG_RESULT` is still the best no-new-wire contact signal available from the
  TMC2209. `TSTEP`, `MSCNT`, and the emitted-STEP ledger describe commands or
  the electrical sequencer, not physical carriage position.
- Homing uses STEP/DIR, not UART velocity control. UART is used for driver
  configuration, per-driver selection, `SG_RESULT`, and phase handling.
- Both RHO drivers share STEP/DIR. The active driver follows STEP/DIR while the
  inactive driver remains energized at 256 microsteps in a phase-correcting
  `VACTUAL=+/-1` hold. The inactive driver is restored to STEP/DIR and its saved
  `MSCNT` phase after the active pass.
- The counterweight pass runs first and the primary RHO pass runs last, so the
  more back-drive-prone primary side is not disturbed by a later pass.
- Every approach is bounded, UART reads are CRC-checked, three consecutive
  UART failures abort, and normal driver settings are restored only after
  verified success.
- The current daughterboard mapping is primary RHO address 0, RHO-CW address
  1, and theta address 2. Both RHO `GCONF.SHAFT` settings are normal; direction
  is set by the installed motor-lead polarity.

What is not established:

- No candidate has completed the required ten cold/warm cycles with zero
  misses, false triggers, or physical desynchronization.
- At least one RHO-CW pass visibly stopped short. One later trial was disturbed
  by the operator, so it cannot be used as evidence either way.
- Primary RHO and RHO-CW were observed out of sync after testing, and the
  primary appeared not fully homed while RHO-CW was at home. There is no
  encoder evidence that distinguishes missed mechanical motion from physical
  disturbance.
- Both RHO UART addresses now respond, but UART presence does not prove that
  either motor coil is connected or that the carriage moved. The RHO-CW driver
  showed both open-load flags at standstill under StealthChop. The
  [TMC2209 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/tmc2209_datasheet_rev1.09.pdf)
  says standstill flags alone cannot establish an open coil; verify CW motion
  and inspect loaded-run diagnostics before trusting a trial.
- The first assembled-table artifact captures a complete outward CW runway,
  but no inward contact event. Its numeric settings are not measured optima.

Begin with one confirmed-zero trial and capture a complete trace. Re-bracket
current, speed, trigger ratio, and vote count from the measured SG separation.
The host script now polls `/api/tuning/homing/trace` during motion and rejects
any missing sample index; the firmware's 768-sample ring can otherwise
overwrite the first motor's readings before the trial ends.

### First assembled-table trial (2026-09-14 UTC)

The 75% trigger, five-sample vote, and 600 ms minimum-travel trial used 153 mA
actual homing current and a 6 mm/s commanded runway. The CW driver answered
all 53 UART reads. Its outward STEP count reached the 8 mm runway, but 41 of
53 `SG_RESULT` samples were 2, three were 0, seven were 6, and two were 18.
Only four of 32 samples in the second half reached the firmware's minimum
healthy-load value of 4; the guard requires eight. Firmware stopped before
either inward approach or any main-RHO motion, returned `HOMING_FAILED`
(`RUNWAY_LOAD_INV`, axis 2), and verified both rho power stages off.

The local worktree artifact
`tuning-recordings/rho-homing-20260913/20260914T030511Z-rho-home-p75-n5-result.json`
contains all 53 samples and the Antlion recording. A-weighted median levels
were −60.68 dBFS before motion, −62.36 dBFS during the CW runway, and
−63.95 dBFS afterward. Brief onset and stop sounds do not establish that the
CW carriage moved. The trial did not reach a contact event, so it cannot tune
the trigger percentage or vote count.

The CW mechanism may have stayed at zero, moved outward, or slipped. Low
homing current, reversed motor direction, a jam, and an electrical connection
fault remain plausible. The operator must inspect CW's physical position and
re-establish both mechanisms at zero before another inward command. Verify
positive STEP/DIR direction and loaded CW motion before changing SG
thresholds. No camera was used for this trial. Keep automatic boot homing off.

## Decision

Use polled `SG_RESULT` as the primary RHO contact signal. The TMC2209 does not
contain a rotor-position or STEP-arrival sensor. `TSTEP` reports the interval
between commanded STEP pulses, `MSCNT` reports the driver's electrical
microstep sequencer, and the controller's STEP ledger reports emitted pulses;
none proves that the rotor or mechanism moved. They are valuable validity and
overtravel checks, but cannot replace StallGuard load sensing.

No DIAG wire is required. During homing the ESP32 polls `SG_RESULT` over UART,
and treats a sustained **drop** relative to the same approach's load baseline
as contact. The Antlion microphone is independent corroboration: hard-stop
contact should create a time-correlated acoustic rise, but room noise alone
must never trigger homing.

## Implemented provisional homing profile

Normal acoustic settings must not change homing behavior. Both quiet RHO
profiles therefore transition to one dedicated profile, home, then restore and
read back the exact prior settings.

| Control | Current implementation |
|---|---:|
| Run and hold current | 150 mA requested |
| Current range | `VSENSE=1`, external 0.11 ohm shunts |
| Chopper | StealthChop, `TPWMTHRS=0` |
| CoolStep | off |
| External microsteps | 8, interpolated to 256 |
| Outward runway | 8 mm |
| Coarse inward speed | 6 mm/s |
| Verification backoff | 4 mm |
| Precision inward speed | 6 mm/s |
| Trigger | 75% of adaptive normal-load `SG_RESULT` |
| Precision vote count | 5 fresh full-step samples in a 9-sample window |
| Coarse vote count | 5 fresh full-step samples in a 9-sample window |
| Precision minimum travel | 600 ms, also constrained by return distance |
| Maximum commanded contact overrun | 1 mm |

These are the values currently compiled or stored by the firmware, not tuned
recommendations. A saved tuning record can override the three detector fields
(`triggerPercent`, `consecutiveSamples`, and `minimumTravelMs`), so record the
live `/api/tuning` response before every resumed trial.

The two RHO motors are homed separately. Firmware keeps the inactive stage
energized but decouples it from shared STEP/DIR through the slow `VACTUAL`
phase-hold described above, then swaps roles. It records motor, phase, elapsed
time, emitted steps, raw `SG_RESULT`, and UART validity for every sample.

## Commissioning safety envelope

Start only after manually placing both mechanisms at physical home and using
the UI's explicit RHO Commissioning confirmation. In this mode each motor:

1. Moves 8 mm outward to establish constant-velocity runway.
   Before reversing, the firmware requires eight valid moving `SG_RESULT`
   samples at or above 4 in the second half of that
   runway. A motor that consumes STEP pulses without convincing load feedback
   stops the trial outward of zero.
2. Approaches inward with an absolute command cap of 9 mm.
3. Stops on the filtered `SG_RESULT` drop or after at most 1 mm of commanded
   hard-stop overrun.
4. Moves 4 mm outward and requires load recovery.
5. Re-approaches at 6 mm/s and requires contact within 1 mm of the expected
   four-millimetre return.

The firmware subtracts any coarse-approach overrun from the precision-pass
travel cap. Both inward approaches share one 1 mm allowance relative to the
confirmed zero; the former fixed five-millimetre precision cap could spend a
second millimetre after a late coarse trigger. It also requires the sum of
both contact errors to end within 1 mm of the confirmed zero. Otherwise two
early triggers could pass the individual checks and leave the mechanism 2 mm
out. The host report checks both combined conditions too.

The cap is valid only because commissioning began at a manually confirmed
zero. Production boot homing begins at an unknown position and therefore
cannot infer a one-millimetre physical overrun bound from STEP count alone. If
that stronger production guarantee is required even when UART sensing fails,
it needs an independent physical position/end-stop signal; software and audio
cannot provide it.

Any timeout, UART error run, early trigger, inconsistent return, driver fault,
more than 1 mm of commanded overrun, or disagreement between the two approaches
fails closed. Do not continue a sweep after failure: leave the stages disabled
and manually re-establish zero.

## Run one trial

Install the RHO service image, switch to RHO Commissioning at the manually
confirmed origin, and run:

```bash
python3 scripts/rho_homing_tuner.py \
  --trigger-percent 75 \
  --consecutive-samples 5 \
  --minimum-travel-ms 600
```

The script records audio before starting motion, waits for `HOMING_REVIEW` or
`HOMING_FAILED`, collects `/api/tuning/homing/trace` throughout the run, and
computes for each
motor's coarse and precision approach:

- normal-load median, minimum, and terminal `SG_RESULT`;
- valid-UART fraction;
- terminal emitted-step count and commanded overrun;
- distance error from expected contact;
- A-weighted audio level at the terminal SG event versus the preceding sound.

If the instrument checks pass, the script leaves the controller in
`HOMING_REVIEW`. The operator must check the physical position before calling
`/api/home/confirm` with `successful=true`. The script rejects a failed result,
which disables both RHO stages. Microphone evidence and STEP bounds do not
establish physical position after missed steps.

## Threshold search

Use the implemented 75% / 5-vote / 600 ms profile only as the first reference
trace. Then tune one dimension at a time:

1. Trigger percentage: bracket 55%, 65%, and 75%. Reject any value that fires
   before `runway - 1 mm`, misses contact, or loses separation between normal
   and terminal SG distributions. Higher percentages are more sensitive.
2. Vote count: among valid thresholds, bracket 5, 9, and 13 fresh full-step
   samples. The detector evaluates a majority in a `2*N-1` window. Select the
   smallest value that never triggers in free travel and keeps maximum overrun
   at or below 1 mm. `SG_RESULT` refreshes once per full step, so elapsed time
   depends on motor speed; do not infer it from UART polling cadence alone.
3. Minimum precision travel: use at least the acceleration/settling interval;
   bracket 400, 600, and 800 ms. It must exclude the initial transient without
   masking a real contact.
4. Only if SG separation is poor, bracket homing velocity and current. Keep
   CoolStep off and use a fixed current. Do not reuse normal-motion current as
   an implicit homing parameter.

Select thresholds independently for RHO and RHO-CW if their distributions
differ. The current settings schema shares detector parameters, so production
must use the stricter common intersection until per-motor fields are added.

## Qualification

For each selected acoustic profile, perform at least ten complete homing cycles
including cold and warm driver starts. Require:

- zero false triggers and zero missed contacts;
- all UART samples valid except isolated retries, never three consecutive
  failures;
- coarse and precision terminal positions within 1 mm for both motors;
- maximum commanded overrun no greater than 1 mm;
- clear separation between normal-load and terminal SG distributions;
- backoff SG recovery on every cycle;
- no driver, thermal, short, undervoltage, or planner fault;
- exact readback restoration of the incoming acoustic settings;
- the post-home acoustic regression still passes that profile's threshold.

Because the dedicated homing profile is intended to erase incoming-state
history, results should not depend on whether the source profile is −60, −63,
or the best-effort −66 tier. If they do, fix configuration/preconditioning
state leakage before creating separate threshold sets.

## Restart checklist

1. Power down before changing any motor or driver connection.
2. Install two known-good TMC2209 modules and confirm UART replies at RHO 0,
   RHO-CW 1, and theta 2 with the non-motion scan image.
3. Confirm both RHO axes move outward for positive STEP/DIR commands with the
   current normal `SHAFT=0` setting. Correct wiring/polarity before homing; do
   not discover direction by driving into a stop.
4. Confirm the 500 mA RMS motor rating, 0.11 ohm sense-resistor assumption, and
   healthy loaded travel at the candidate homing current.
5. Manually home both mechanisms, press **Set current position as home**, and
   enter RHO Commissioning. Use only known-position bounded trials first.
6. Record `/api/tuning`, both driver dumps, telemetry, errors, the complete
   homing trace, and synchronized Antlion audio for every attempt.
7. Tune each motor's distributions separately. Use the common intersection of
   safe settings until the schema supports per-motor detector values.
8. After a failure or any physical intervention, invalidate the STEP ledger,
   disable both stages, manually re-establish zero, and restart the trial.
9. Complete the full qualification section before enabling production or boot
   homing.
