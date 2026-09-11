# RHO sensorless-homing tuning playbook

## Paused checkpoint (2026-09-10)

RHO homing tuning is **on hold** while theta sound tuning and replacement
RHO-driver work proceed. The implementation is a guarded experimental method,
not a qualified production home. Do not enable automatic boot homing or treat
the current detector values as final.

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
- The RHO-CW motor/driver hardware subsequently failed or was removed. The
  latest normal-firmware preflight found neither RHO UART address, while theta
  at address 2 remained healthy. Two-driver homing cannot be resumed until both
  RHO addresses respond reliably and both mechanisms move under load.
- No durable homing trace/result artifact was captured in
  `tuning-recordings/`; therefore the numeric settings below are an
  implementation checkpoint, not measured optima.

Resume only after installing healthy hardware, confirming addresses 0 and 1,
manually placing both mechanisms at the same physical zero, and capturing a
fresh known-position trace. Start the new search from the checkpoint below,
but re-bracket current, speed, trigger ratio, and vote count rather than
assuming the checkpoint is good.

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
| Precision inward speed | 1.5 mm/s |
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
2. Approaches inward with an absolute command cap of 9 mm.
3. Stops on the filtered `SG_RESULT` drop or after at most 1 mm of commanded
   hard-stop overrun.
4. Moves 4 mm outward and requires load recovery.
5. Re-approaches at 1.5 mm/s and requires contact within 1 mm of the expected
   four-millimetre return.

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
`HOMING_FAILED`, downloads `/api/tuning/homing/trace`, and computes for each
motor's coarse and precision approach:

- normal-load median, minimum, and terminal `SG_RESULT`;
- valid-UART fraction;
- terminal emitted-step count and commanded overrun;
- distance error from expected contact;
- A-weighted audio level at the terminal SG event versus the preceding sound.

It confirms logical zero only when both motors and both approaches pass every
hard check. Otherwise it rejects the review result, which disables the RHO
stages.

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
