# RHO sensorless-homing tuning playbook

## Current checkpoint (2026-09-14 18:18 UTC)

The main RHO motor is connected at driver address 0. A driver answers UART at
RHO-CW address 1, but **no CW motor is connected**. The main-only service image
keeps the CW bridge at `TOFF=0`. The operator confirmed physical zero, then
authorized the Logitech C925e camera as a commissioning-stage position check.
The zero-reference image is `/tmp/rho-home-pretrial-20260914.jpg`; compare the
moving motor housing with the fixed base before and after each trial. The
camera resolves about 0.3 mm, so a matching image cannot prove exact contact.

The current service image uses 250 mA requested homing current, full external
steps, 6 mm/s, 1 ms SG polling, a 75% trigger with five votes, an 8 mm runway,
a 4 mm backoff, and a **shared 2 mm commanded-overrun cap**. It now ignores
coarse SG triggers until 7 mm of inward runway travel and requires the second
approach to agree with the first within 0.5 mm. Ten consecutive known-zero
warm trials passed firmware and camera checks. On one of them, replay of the
old 3 mm arming rule triggers at step 344 of 400, whereas the live 7 mm rule
continued to step 401 and reached the camera reference. **Autonomous
unknown-position RHO homing is not qualified. Keep automatic boot homing off.**
Production firmware also rejects `/api/home` while
`kEnableUnknownPositionRhoHoming=false`; use the RHO service image for bounded
known-origin trials.
The controller was left `IDLE` at logical rho 0 after camera confirmation.

The earlier image with a 3 mm coarse arming point passed six known-zero warm
trials, then false-triggered 3.66 mm before home. The camera showed the motor
housing 11 pixels outward, and firmware rejected that result. Four 1 mm
inward manual jogs returned it to the camera reference before the current
image was uploaded.

What is established:

- `SG_RESULT` remains the TMC2209's available load signal, but it has produced
  confirmed mid-travel false triggers on this assembled mechanism. `TSTEP`,
  `MSCNT`, and the emitted-STEP ledger describe commands or the electrical
  sequencer, not physical carriage position.
- Homing uses STEP/DIR, not UART velocity control. UART is used for driver
  configuration, per-driver selection, `SG_RESULT`, and phase handling.
- Both daughterboard drivers receive shared STEP/DIR. With no CW motor fitted,
  firmware turns off its bridge and verifies `TOFF=0` before main RHO moves.
  The former phase-correcting `VACTUAL=+/-1` hold applies only when both motors
  are fitted.
- Known-zero commissioning approaches have a shared 2 mm commanded-overrun
  cap. UART reads are CRC-checked, three consecutive UART failures abort, and
  normal driver settings are restored only after verified success.
- The daughterboard maps main RHO to address 0, CW to address 1, and theta to
  address 2. Main RHO uses `GCONF.SHAFT=0` with its installed motor-lead
  polarity.

What is not established:

- The new profile has ten warm passes, but no power-cold cycle series and no
  unknown-position physical check. The previous 3 mm arming profile failed on
  its seventh warm repeat.
- The camera is an external test instrument. The ESP32 does not have that
  position measurement during standalone boot homing.
- An unknown-position boot cannot use the known-zero overrun cap to prove a
  physical 2 mm limit. Firmware still needs a safe unknown-origin strategy.

Do not treat the new ten-pass known-origin result as proof of unknown-position
homing. The 7 mm guard rejects all three recorded early coarse false triggers,
but a fixed rail constriction farther inward could generate two agreeing SG
events at the wrong place. Use the camera and command ledger to label each
new trial, and keep the two-pass contact budget explicit.
The host script now polls `/api/tuning/homing/trace` during motion and rejects
any missing sample index; the firmware's 768-sample ring can otherwise
overwrite early runway readings before the trial ends.

### First assembled-table trial (2026-09-14 UTC)

The 75% trigger, five-sample vote, and 600 ms minimum-travel trial used 153 mA
actual homing current and a 6 mm/s commanded runway. The empty CW driver answered
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
−63.95 dBFS afterward. Those sounds cannot represent powered CW travel
because no motor was connected. The trial did not reach a contact event, so
it cannot tune the trigger percentage or vote count.

The operator later clarified that no CW motor was connected. The failed CW
runway therefore says nothing about main-RHO load sensing. The main-only image
must verify address 1 stays disabled before sending main RHO STEP pulses. No
camera was used for this trial. Keep automatic boot homing off.

### First main-only trial (2026-09-14 04:44 UTC)

The main-only image kept address 1 at `TOFF=0` and sent every homing STEP to
main RHO. All 334 SG reads passed UART validation. With 153 mA actual current
at 6 mm/s, the coarse pass stopped at 3,268 steps against a 3,200-step
expected contact. Its moving SG median was 175 and terminal value 114. After
the 4 mm backoff, the precision pass stopped at 1,786 steps against 1,600
expected; its moving median was 184 and terminal value 66. The two passes
spent 0.635 mm of the shared 1 mm commanded overrun allowance.

Antlion audio rose 8.96 dB and 14.47 dB around the two terminal SG events
relative to their adjacent motion windows. These aligned events support
hard-stop contact, but no independent sensor proves final carriage position.
The controller returned `HOMING_FAILED` (failure 7, axis 1) because one
CHOPCONF write lost its local UART echo during restoration. It disabled both
bridges. Main RHO retained the dedicated homing driver settings, so the host
also rejected exact setting restoration. The local artifact is
`tuning-recordings/rho-main-only-20260914/20260914T044428Z-rho-home-p75-n5-result.json`.
The source now checks CHOPCONF readback and retries a transient echo failure;
this fix needs a new hardware trial before the profile can pass.

### Main-only repeat after UART fix (2026-09-14 04:47 UTC)

The repeat passed the host's instrument checks and left firmware in
`HOMING_REVIEW`. All 321 SG reads passed UART validation. The coarse pass
stopped at 3,333 steps, 0.3325 mm beyond the expected contact; its moving SG
median was 178 and terminal value 96. The precision pass stopped at exactly
1,600 steps, with moving median 177 and terminal value 46. Antlion audio rose
12.26 dB and 12.24 dB near those SG events. Firmware restored main RHO's
normal driver settings and kept the empty CW bridge at `TOFF=0`.

The artifact is
`tuning-recordings/rho-main-only-20260914/20260914T044741Z-rho-home-p75-n5-result.json`.
No camera or rotor-position sensor verified the final physical position. The
host therefore did not call `/api/home/confirm`; main RHO remains in
`HOMING_REVIEW`, and 75% / five votes remains a candidate, not a qualified
production setting.

The offline replay command below reproduced the exact coarse and precision
trigger steps in both main-only traces at 75% / five votes:

```bash
python3 scripts/rho_homing_replay.py \
  tuning-recordings/rho-main-only-20260914/*-result.json
```

On the recorded prefixes, 55–70% did not produce a precision trigger before
the original stop. They might trigger later; the recordings end at the
75% event, so replay cannot judge the remaining 1 mm safety budget. The
80–85% five-vote candidates triggered no later than 75%, but a higher
threshold could increase false contacts on rail-load changes. Nine or 13
votes often needed samples beyond the second recording's stop. Keep 75% /
five votes for the next physically checked repeat; do not promote any replay
candidate directly to production.

### Camera-checked main-only sweep (2026-09-14 16:53–17:57 UTC)

The operator allowed the camera check and up to 2–5 mm commanded overrun. We
tested the smallest larger cap, 2 mm. The Antlion recording stayed advisory:
changing room noise sometimes masked a contact, and one false mid-travel load
event produced a 6.64 dB acoustic rise. The ESP32 cannot use either instrument
when it runs by itself.

| Profile | Camera-checked result | Main finding |
|---|---|---|
| 150 mA, 8 microsteps, 6 mm/s, 5 ms polls, 1 mm cap | A pass followed by a false coarse trigger at 2,288/3,200 steps | Camera measured 7 pixels outward, about 2.3 mm from zero. |
| 200 mA, 8 microsteps, 6 mm/s | False coarse trigger at 1,910/3,200 steps | More current did not remove the mid-travel load event. |
| 350 mA, 8 microsteps, 6 mm/s | Precision missed despite camera-confirmed contact | Alternating high/low SG values defeated the original vote rule. |
| 350 mA, full steps, 6 mm/s | One pass, then two precision misses | Full steps changed the signal but did not establish repeatability. |
| 250 mA, full steps, 6 mm/s, 5 ms polls, 1 mm cap | Three passes, then a coarse miss at 0.98 mm commanded overrun | The one-millimetre cap left too little contact-sampling time. |
| 250 mA, full steps, 8 mm/s, 5 ms polls, 1 mm cap | Precision miss at 0.98 mm total commanded overrun | Faster travel produced fewer SG reads per millimetre. |
| 250 mA, full steps, 6 mm/s, 1 ms polls, 2 mm cap | Six passes, then a false coarse trigger at 217/400 steps | The camera measured 11 pixels outward, about 3.7 mm from zero. |

The 1 ms poll captured 539–602 valid SG readings per passing trial, compared
with about 300 at 5 ms. Those six passes ended at the camera's zero reference;
coarse triggers ranged from 396 to 408 steps, and precision triggers ranged
from 200 to 220 steps at 50 steps/mm. The seventh trial stopped at 217 inward
steps after the 400-step outward runway. Firmware's known-origin window caught
the false trigger and left both bridges off. In an unknown-position boot, the
same SG event would not have that independent positional check.

The current detector also accepts two near-zero SG readings in a nine-sample
window because the real stop can alternate near-zero and high values. Offline
replay reproduced the recorded full-step missed contacts, but the live
six-of-seven result shows that detector tuning alone has not removed false
positives. Increasing the overrun cap helps missed contacts, not false early
contacts. Keep the current profile confined to the RHO service image.

The lossless raw SG and firmware-result JSON for the relevant runs live under
`tuning-recordings/rho-main-only-20260914/`; the `17:57:22` result records the
final false trigger. Audio and camera JPEGs remain local test instruments.

### Later coarse arming and tighter agreement (2026-09-14 18:15–18:19 UTC)

An 8 mm outward runway means a genuine end-stop contact cannot occur before
roughly 8 mm of inward return if the motor actually made the outward move.
The original 3 mm coarse arming point accepted load events at 4.34, 4.78,
and 5.72 mm on recorded traces. Replaying those traces with a 7 mm arming
point rejects all three; the previously camera-checked contacts still replay
near the expected 8 mm return. The one-millimetre margin is provisional and
does not prove the motor physically completed its outward runway.

In ten new known-zero warm trials, the coarse trigger was 392–420 of 400
expected full steps, and the return trigger was 196–215 of 200 expected.
All ten final camera images matched the zero reference, all host instrument
checks passed, and no UART/driver fault occurred. The largest per-pass
commanded overrun was 0.4 mm; the two-pass agreement error was at most
0.3 mm. One trial's SG trace would have triggered the old detector at step
344, but the new arming gate ignored it and stopped at step 401. This is
direct evidence that the gate rejected a real mid-travel load event, not yet
evidence that no later false event can occur.

The second pass already acts as a confirmation: after 4 mm outward backoff,
it must reproduce the first command coordinate within 0.5 mm. A single
disagreeing result currently fails closed. A future bounded third attempt
could tolerate one transient false result, but an unlimited retry loop is
unsafe, and agreement at a fixed constriction would still be a false home.
Any retry design must cap cumulative inward travel and contact overrun, then
be tested against deliberately introduced disagreement before use at boot.

## Decision

The current experiment polls `SG_RESULT` as the RHO load signal, but its
mid-travel false triggers prevent production use. The TMC2209 does not contain
a rotor-position or STEP-arrival sensor. `TSTEP` reports the interval between
commanded STEP pulses, `MSCNT` reports the driver's electrical microstep
sequencer, and the controller's STEP ledger reports emitted pulses; none proves
that the rotor or mechanism moved. They remain useful validity and travel
checks.

No DIAG wire is required for these tests. DIAG would reflect the driver's
StallGuard decision, not add a separate position sensor. During homing the
ESP32 polls `SG_RESULT` over UART and looks for a load drop relative to that
approach's baseline. The Antlion microphone helps label test events, but the
standalone ESP32 cannot use it at boot and room noise cannot establish contact.

## Implemented provisional homing profile

Normal acoustic settings must not change homing behavior. The main motor uses
one dedicated profile for homing, then firmware restores and reads back its
normal settings. Firmware keeps the empty CW channel disabled throughout.

| Control | Current implementation |
|---|---:|
| Run and hold current | 250 mA requested |
| Current range | `VSENSE=1`, external 0.11 ohm shunts |
| Chopper | StealthChop, `TPWMTHRS=0` |
| CoolStep | off |
| External microsteps | 1, interpolated to 256 |
| Outward runway | 8 mm |
| Coarse inward speed | 6 mm/s |
| Verification backoff | 4 mm |
| Precision inward speed | 6 mm/s |
| Trigger | 75% of adaptive normal-load `SG_RESULT` |
| Precision vote count | 5 fresh full-step samples in a 9-sample window |
| Coarse vote count | 5 fresh full-step samples in a 9-sample window |
| Precision minimum travel | 650 ms, also constrained by return distance |
| Maximum commanded contact overrun | 2 mm shared between passes |
| Coarse detector arming | after 7 mm inward travel from the 8 mm runway |
| Second-pass agreement | within 0.5 mm of the first trigger coordinate |
| SG polling | 1 ms, with duplicate full-step readings discarded |

These are the values currently compiled or stored by the firmware, not tuned
recommendations. A saved tuning record can override the three detector fields
(`triggerPercent`, `consecutiveSamples`, and `minimumTravelMs`), so record the
live `/api/tuning` response before every resumed trial.

The current build homes main RHO only. It records phase, elapsed time, emitted
steps, raw `SG_RESULT`, and UART validity for each sample. The host trial
requires the CW bridge disabled before and after motion and rejects any CW
homing trace sample.

## Commissioning safety envelope

Start only after confirming main RHO at physical home and using the UI's
explicit RHO Commissioning confirmation. In this mode the main motor:

1. Moves 8 mm outward to establish constant-velocity runway.
   Before reversing, the firmware requires eight valid moving `SG_RESULT`
   samples at or above 4 in the second half of that
   runway. A motor that consumes STEP pulses without convincing load feedback
   stops the trial outward of zero.
2. Approaches inward with an absolute command cap of 10 mm.
3. Stops on the filtered `SG_RESULT` drop or after at most 2 mm of commanded
   hard-stop overrun.
4. Moves 4 mm outward and requires load recovery.
5. Re-approaches at 6 mm/s and requires its SG trigger within 0.5 mm of the
   first trigger coordinate after the four-millimetre backoff.

The firmware subtracts any coarse-approach overrun from the precision-pass
travel cap. Both inward approaches share one 2 mm allowance relative to the
confirmed zero; the former fixed five-millimetre precision cap could spend a
second full allowance after a late coarse trigger. It also requires the sum of
both contact errors to end within 2 mm of the confirmed zero. Otherwise two
early triggers could pass the individual checks and leave the mechanism 4 mm
out. The host report checks both combined conditions too.

The cap is valid only because commissioning began at a manually confirmed
zero. Production boot homing begins at an unknown position and therefore
cannot infer a two-millimetre physical overrun bound from STEP count alone. If
that stronger production guarantee is required even when UART sensing fails,
it needs an independent physical position/end-stop signal; software and audio
cannot provide it.

Any timeout, UART error run, early trigger, inconsistent return, driver fault,
more than 2 mm of commanded overrun, or disagreement between the two approaches
fails closed. Do not continue a sweep after failure: leave the stages disabled
and manually re-establish zero.

## Run one trial

Install the main-only RHO service image. Confirm that `/api/tuning` reports
`homing.companionMotorEnabled=false`, that the CW driver dump reports
`motorConfigured=false` and `TOFF=0`, then verify main RHO against the
operator-confirmed camera reference and run this known-zero bounded trial:

```bash
python3 scripts/rho_homing_tuner.py \
  --expected-motors main \
  --rho-start-mm 0 --companion-start-mm 0 \
  --trigger-percent 75 \
  --consecutive-samples 5 \
  --minimum-travel-ms 650
```

The script records audio before starting motion, waits for `HOMING_REVIEW` or
`HOMING_FAILED`, collects `/api/tuning/homing/trace` throughout the run, and
computes for the main motor's coarse and precision approach:

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

The old 3 mm arming profile at 75% / five votes / 650 ms failed on its
seventh repeat. The newer 7 mm arming / 0.5 mm agreement profile has ten
known-zero warm passes, including one live rejection of a mid-travel SG
event. It remains an unqualified unknown-origin candidate.

Next, capture SG for at least 0.5 mm *after* a candidate event in a
known-origin trial, still under a separately checked total command cap. That
will show whether the signal recovers after a rail constriction but stays
distinctive at the true stop. The current trace stops at the candidate, so it
cannot answer that question. Replay any proposed rule against both false
traces and all camera-labeled real-stop traces before loading new firmware.
Consider polling `PWM_SCALE_SUM` as a second load-related diagnostic if SG
continuation does not separate the cases; it is not a position measurement.

The 7 mm gate rejects the 217/400-step false event and two earlier false
traces. Before bracketing threshold, vote count, current, or velocity again,
test whether a fixed constriction beyond 7 mm can produce two agreeing
triggers. Keep CoolStep off during comparison so a changing coil current does
not confound SG calibration. Use
the known-zero firmware window and the camera on each trial. Stop at the first
false trigger, miss, driver fault, or camera disagreement.

Tune main-RHO thresholds alone while CW has no motor. When a CW motor is
installed, qualify its direction, current, and SG distribution before
re-enabling paired homing. The settings schema still shares detector values,
so paired production use will need a safe common setting or per-motor fields.

## Qualification

For the dedicated homing profile, perform at least ten complete homing cycles
including power-cold and warm driver starts. Require:

- zero false triggers and zero missed contacts;
- all UART samples valid except isolated retries, never three consecutive
  failures;
- main-RHO coarse and precision terminal commands within the 2 mm
  known-origin window, and the final camera image aligned with zero;
- maximum shared commanded overrun no greater than 2 mm;
- coarse arming only after 7 mm of inward runway travel and two triggers
  agreeing within 0.5 mm;
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
2. Confirm UART replies at main RHO 0 and CW 1. Leave the empty CW driver at
   `TOFF=0`; confirm that state through `/api/tuning/dump/rho-companion`.
3. Confirm main RHO moves outward for positive STEP/DIR with `SHAFT=0`.
   Correct wiring/polarity before homing; do not discover direction by driving
   into a stop.
4. Confirm the 500 mA RMS motor rating, 0.11 ohm sense-resistor assumption, and
   healthy loaded travel at the candidate homing current.
5. Manually home main RHO, press **Set current position as home**, and
   enter RHO Commissioning. Use only known-position bounded trials first.
6. Record `/api/tuning`, both driver dumps, telemetry, errors, the complete
   homing trace, and synchronized Antlion audio for every attempt.
7. Tune only main RHO's SG distribution. Leave CW motor configuration off.
8. After a failure or any physical intervention, invalidate the STEP ledger,
   disable both stages, manually re-establish main RHO zero, and restart.
9. Complete the full qualification section before enabling production or boot
   homing.
