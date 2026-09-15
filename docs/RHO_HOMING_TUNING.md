# RHO sensorless-homing tuning playbook

## Current experiment (2026-09-15 UTC)

### Rolling search and backoff experiment, after 03:20 UTC

The task-timer trials below do not establish an optimal backoff. In particular,
three agreeing contacts can still be false: the 03:34 trial agreed within
0.36 mm about 22 mm outward of home. The independent known-home check rejected
it and the camera showed the mechanism outward. Do not enable unknown-origin
boot homing based on contact consensus alone.

| Trial UTC | Known start / requested backoff | Outcome |
|---|---|---|
| 03:30:25 | 9.0425 / 10 mm | Six contacts; final span 0.36 mm; camera matched home. |
| 03:32:21 | 25 / 15 mm | Eleven contacts; final span 0.3125 mm; camera matched home. |
| 03:34:55 | 25 / 25 mm | Three false contacts; final span 0.36 mm, last point 21.7875 mm short. Rejected. Actual backoffs were only 11.355 and 11.5725 mm because early triggers limited outward room. |

A homing-only TG1/T1 hardware-timer experiment follows these trials. Keep the
same motor profile and detector initially to separate pulse-timing effects
from parameter changes. Record `pulseSource` in each result. Audit the entire
ISR call path for IRAM/ROM residency before running: Arduino's microsecond
delay and the toolchain's out-of-line atomic-bool load are flash-resident in
this build. Normal planned motion retains its existing timer.

The first hardware-timer trial (03:43:51 UTC) recovered from the recorded
21.7875 mm outward position with a 10 mm backoff. Contacts relative to known
zero were -0.1325, +0.24, +0.32, and +0.245 mm; the final three spanned
0.08 mm. Camera registration matched the home reference (dx=0, dy=0,
NCC=0.941), and the result was confirmed. In 25..35 ms cruising trace windows,
the 5th-percentile commanded STEP rate rose from 2,801 steps/s in the preceding
failed task-timer trial to 4,765 steps/s; target 4,800 steps/s. This is command
counter evidence, not a logic-analyzer measurement of individual pulse jitter.

| Hardware-timer trial UTC | Known start / backoff | Contacts / final span | Camera |
|---|---|---|---|
| 03:43:51 | 21.7875 / 10 mm | 4 / 0.08 mm | Home |
| 03:45:27 | 25 / 8 mm | 3 / 0.11 mm | Home |
| 03:46:23 | 25 / 15 mm | 3 / 0.2825 mm | Home |
| 03:47:23 | 25 / 25 mm | 3 / 0.0825 mm | Home |

These are screening runs. Repeat the leading backoff at distinct starts and
compare warm/cold runs before calling it reliable. Do not select a larger
backoff or relax agreement merely to rescue the older task-timer results.

The user requested a rolling buffer of three SG contact points, allowing an
early candidate to disappear on the next return. Firmware now continues the
return past that candidate toward the original commissioning bound. It aborts
on a missing contact at that bound, communication failure, or twelve attempts
without agreement. Three agreeing contacts away from the known zero still
fail the commissioning check.

Use `--backoff-mm` with the host script, or `verificationBackoffMm` on
`POST /api/tuning/homing`, to select 8..50 mm without reflashing. Start the
comparison with 8, 10, 15, and 25 mm. Firmware clamps each backoff to the
available outward distance from the first inward approach's starting point,
accounting for previously consumed overrun. Start comparison trials at least
25 mm out so the travel clamp does not erase the differences between choices.

The command ledger increases inward from the first approach's start. Trace
field `o` records each leg's starting coordinate; `o+s` gives an inward
candidate's coordinate. Record the three-point span in that common frame.
An arbitrary label such as 425 mm changes the offset and sign, not agreement.
Keep the camera-checked physical starting distance separate from this label.

For commissioning, keep a high-water mark of commanded inward progress beyond
the known zero. Each approach may extend that mark by at most 2 mm, with at
most 5 mm total extension. Returning farther than a false candidate spends
remaining search distance; it does not spend end-stop overrun before reaching
the known zero. Backing out cannot refund the high-water mark. These limits
still assume the motor executes the outward movement and does not lose steps
before contact. Unknown-origin boot homing remains disabled.

Compare backoffs by camera-verified returns, false consensus, cap failures,
approach count, commanded overrun, and cycle time. Record actual backoff from
successive leg origins, since a requested 25 mm can be clamped. Check the
return's cruising STEP rate and SG baseline before changing the tolerance.
Keep 0.4 mm provisional and validate the chosen profile on separate starts.

The first rolling trial (03:24:02 UTC, 8 mm backoff, 7.2075 mm known start)
recorded contacts at -4.8275, -1.31, -0.17, -0.285, and +0.0025 mm relative
to the known zero. Firmware continued past the first two candidates and
accepted the last three, span 0.2875 mm. The final camera image matched the
home reference at zero integer-pixel offset, correlation 0.941; confirmation
through the API followed that check. This is one demonstrated recovery from
early triggers, not a reliability qualification.

The 03:26:09 UTC trial used a 25 mm start and 10 mm backoff. It exhausted
twelve contacts, ending 9.0425 mm short by the ledger and outward in the
camera view. The fixed 7.8 mm retry arming distance allowed triggers before
returning to the previous candidate, including a 1.96 mm outward regression.
The next image sets retry arming to the greater of the configured time gate's
distance and the actual backoff minus the agreement tolerance. Metadata
`retryArmingTracksBackoff` distinguishes the two versions for replay.

### Earlier fixed-return experiment

Main RHO remains the only fitted motor under test. Keep unknown-origin boot
homing disabled: we have not qualified the assembled table across its travel.

The current experiment uses 500 mA requested (490 mA reported by current-scale
calculation), u8 with interpolation, 12 mm/s, an 8 mm runway and 8 mm backoff.
The live detector setting is 85%, five votes in nine fresh samples, and a
650 ms precision travel gate. Normal motion retains its separate quiet profile.
Current readback is a register-derived estimate, not an ammeter measurement.

We now require three consecutive contact coordinates within a **0.4 mm total
span**, with at most six approaches. This differs from allowing each neighboring
pair to differ by 0.4 mm; that would admit cumulative drift. The user authorized
separate pass allowances: each approach gets up to 2 mm, subject to a shared
5 mm positive-overrun budget. Early triggers do not refund spent overrun.
These bounds assume the outward backoff moves the commanded distance. SG
recovery during backoff supports that assumption but cannot measure distance.

The 0.4 mm agreement span remains provisional. Select it from repeatability
data, then test it on separate starts and thermal conditions. Compare genuine
home repetitions with false contacts. A reproducible rail obstruction could
satisfy a tight consensus, so agreement alone does not establish physical home.

New evidence:

| Trial UTC | Profile/result |
|---|---|
| 00:20:52 | Earlier 250 mA/u1/6 mm/s profile false-triggered 132.72 mm before home on the 200 mm start. Known-position bounds rejected it. |
| 00:23:50 | Recovery from that position false-triggered with 78.04 mm remaining. |
| 02:11:33–02:24:58 | u8, 250/500 mA, 6 mm/s: added 0.5 mm persistence probes rejected contact or exhausted the cap. Removed that probe from firmware. |
| 02:36:59 | New 12 mm/s multipass strategy: first three coordinates spanned 0.44 mm; a fourth approach exhausted its 2 mm cap. Failed. |
| 02:38:29 | Three-contact firmware and host pass; span 0.2675 mm, positive overrun 0.35 mm. Camera matched the zero reference; confirmed through the API. |
| 02:39:34 | Firmware accepted on pass five; final three coordinates spanned 0.225 mm. Host rejected an earlier pass because it compared SG with the wrong baseline. Preserve that original failed report. |
| 02:44:00 | Marked three-contact pass; span 0.1625 mm. Camera matched home; confirmed through the API. |
| 02:54:42 | From a camera-checked 10 mm start, coarse SG triggered after 7.17 mm of the expected 18 mm inward travel: approximately 10.83 mm short. All 159 UART reads were valid. Firmware rejected it; camera and operator confirmed it was not home. |

After the last failure, a slow 10 mm inward manual jog left the camera marker
two pixels outward. A separate 1 mm inward jog returned it to the home
reference (zero integer-pixel offset, correlation 0.964). Only then was Set
Home called. This recovery is not an automatic-homing pass. The ledger's
11 mm recovery command implies about 0.17 mm nominal overshoot, subject to
unmeasured step loss and camera resolution.

An intervening 10 mm trial rebooted the controller during capture. The cause
is unconfirmed; do not call it a trace-index race or a proven heap failure.
The trace endpoint now returns at most 256 samples per request rather than
768, with smaller response allocation; the host still polls and detects any
lost indices. Status reports the ESP32 reset reason, and aborted trials retain
partial evidence. A repeat with the smaller endpoint did not reboot, but
produced the 02:54:42 false contact above.

The latest trace also shows STEP-rate variation after the acceleration ramp:
173 emitted steps over one 36 ms window versus 129 over another, at a nominal
4800 steps/s. SG fell during some slower windows. This correlation does not
establish causation. Homing currently uses the task-dispatched ESP timer,
whose callbacks can be delayed ([Espressif timer documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/system/esp_timer.html)).
SG depends on velocity as well as current and load ([TMC2209 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/tmc2209_datasheet_rev1.09.pdf)).
Measure timing before selecting a final SG threshold. Homing also retained
uncalibrated instruction-loop STEP/DIR delays after normal motion switched to
explicit delays; the source now uses the same 2 microsecond STEP pulse and
DIR setup constants for both paths. This hardening is not proof that rejected
pulses caused the failed trial.

Artifacts are under `tuning-recordings/rho-main-only-20260915/` with the UTC
prefixes above. The host now records each approach separately through trace
field `n`. New firmware adds `b` and `h` only to the stopped contact sample:
the detector's actual baseline and threshold. A cap stop has no contact marker.
Use these markers when measuring candidate scatter; do not count a failed
approach's terminal step count as an SG-detected contact.

The current camera view resolves approximately 2.7 pixels/mm from the observed
10 mm outward jog. Use `/tmp/rho-pre-jog-reference.jpg` for this view; the older
reference no longer registers well. New zero images match at zero integer-pixel
offset with normalized correlation above 0.97. This cannot certify sub-pixel
physical accuracy or prove that a 0.4 mm tolerance is sufficient.

Manual recovery now restores and verifies the fitted RHO bridge before an
unhomed jog. After a failed homing cycle, a 10 mm outward manual jog moved the
mechanism in the camera view. The earlier apparent lack of motion at the
service image's low default speed was not proof of missed steps.

## Historical checkpoint (2026-09-14 18:18 UTC)

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
| Run and hold current | 500 mA requested (provisional) |
| Current range | `VSENSE=1`, external 0.11 ohm shunts |
| Chopper | StealthChop, `TPWMTHRS=0` |
| CoolStep | off |
| External microsteps | 8, interpolated to 256 |
| Outward runway | 8 mm |
| Coarse inward speed | 12 mm/s |
| Verification backoff | 8 mm |
| Precision inward speed | 12 mm/s |
| Trigger | 85% live setting; compiled fallback remains 75% |
| Precision vote count | 5 fresh full-step samples in a 9-sample window |
| Coarse vote count | 5 fresh full-step samples in a 9-sample window |
| Precision minimum travel | 650 ms, also constrained by return distance |
| Maximum commanded contact overrun | 2 mm per approach, 5 mm shared |
| Coarse detector arming | after 7 mm inward travel from the 8 mm runway |
| Contact agreement | last three coordinates within a 0.4 mm span; six approaches maximum |
| SG polling | 1 ms, with duplicate full-step readings discarded |

These are the values currently compiled or stored by the firmware, not tuned
recommendations. A saved tuning record can override the three detector fields
(`triggerPercent`, `consecutiveSamples`, and `minimumTravelMs`), so record the
live `/api/tuning` response before every resumed trial.

The current build homes main RHO only. It records phase, elapsed time, emitted
steps, raw `SG_RESULT`, and UART validity for each sample. The host trial
requires the CW bridge disabled before and after motion and rejects any CW
homing trace sample.

## Historical fixed-return commissioning safety envelope

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
4. Moves 8 mm outward and requires load recovery.
5. Re-approaches at 12 mm/s, then repeats the backoff/return until three
   consecutive ledger contact coordinates span at most 0.4 mm, or six total
   approaches have been attempted.

Each inward approach has a 2 mm allowance, reduced if less than that remains
in the shared 5 mm positive-overrun budget. Early contacts cannot refund
positive overrun. Each contact coordinate is computed by adding emitted
inward steps and subtracting the outward backoff. These are commanded
coordinates, not measured physical coordinates: steps commanded into the
stop can accumulate apparent inward drift while the carriage stays still.
Do not use agreement alone to prove home, or loosen its tolerance to admit a
known mid-travel false trigger. The host validates each marked approach,
the cumulative allowance, and the final three-contact span independently.

The cap is valid only because commissioning began at a manually confirmed
zero. Production boot homing begins at an unknown position and therefore
cannot infer a two-millimetre physical overrun bound from STEP count alone. If
that stronger production guarantee is required even when UART sensing fails,
it needs an independent physical position/end-stop signal; software and audio
cannot provide it.

Any timeout, UART error run, early trigger, inconsistent return, driver fault,
more than 2 mm of commanded overrun in a pass, more than 5 mm accumulated
positive overrun, or failure to find three agreeing contacts
fails closed. Do not continue a sweep after failure: leave the stages disabled
and re-establish zero with operator confirmation or the authorized bounded,
camera-checked manual recovery. Never call Set Home at a failed endpoint.

## Run one trial

Install the main-only RHO service image. Confirm that `/api/tuning` reports
`homing.companionMotorEnabled=false`, that the CW driver dump reports
`motorConfigured=false` and `TOFF=0`, then verify main RHO against the
operator-confirmed camera reference and run this known-zero bounded trial:

```bash
python3 scripts/rho_homing_tuner.py \
  --expected-motors main \
  --rho-start-mm 0 --companion-start-mm 0 \
  --trigger-percent 85 \
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
