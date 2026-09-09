# RHO sensorless-homing tuning playbook

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

## Fixed homing profile

Normal acoustic settings must not change homing behavior. Both quiet RHO
profiles therefore transition to one dedicated profile, home, then restore and
read back the exact prior settings.

| Control | Initial value |
|---|---:|
| Run and hold current | 450 mA requested, CS14 / about 459 mA nominal |
| Current range | `VSENSE=1`, external 0.11 ohm shunts |
| Chopper | StealthChop, `TPWMTHRS=0` |
| CoolStep | off |
| External microsteps | 8, interpolated to 256 |
| Outward runway | 4 mm |
| Coarse inward speed | 4 mm/s |
| Verification backoff | 4 mm |
| Precision inward speed | 1.5 mm/s |
| Initial trigger | 65% of adaptive normal-load `SG_RESULT` |
| Initial debounce | 18 validated samples |
| Precision minimum travel | 600 ms |

The two RHO motors are tuned and homed separately. Firmware disables one power
stage and verifies `CHOPCONF.TOFF=0`, drives the other through the shared
STEP/DIR pins, then swaps roles. It records motor, phase, elapsed time, emitted
steps, raw `SG_RESULT`, and UART validity for every sample.

## Commissioning safety envelope

Start only after manually placing both mechanisms at physical home and using
the UI's explicit RHO Commissioning confirmation. In this mode each motor:

1. Moves 4 mm outward to establish constant-velocity runway.
2. Approaches inward with an absolute command cap of 5 mm.
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
  --trigger-percent 65 \
  --consecutive-samples 18 \
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

Use the initial 65% / 18-sample / 600 ms trial first. Then tune one dimension
at a time:

1. Trigger percentage: bracket 55%, 65%, and 75%. Reject any value that fires
   before `runway - 1 mm`, misses contact, or loses separation between normal
   and terminal SG distributions. Higher percentages are more sensitive.
2. Debounce: among valid thresholds, test 12, 18, and 24 samples. Select the
   shortest run that never triggers in free travel and keeps maximum overrun
   at or below 1 mm. At a 20 ms sample interval, 18 samples span about 360 ms.
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

