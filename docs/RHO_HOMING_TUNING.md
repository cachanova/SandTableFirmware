# RHO sensorless-homing tuning playbook

## Current experiment (2026-09-15 UTC)

### Unknown-position startup qualification

The user requested qualification and boot enable, followed by cleanup/merge
and main-only acoustic retuning. The startup trial build keeps automatic boot
motion off while exercising the production entry with an independent known
position cap. Use `esp32dev_rho_startup_trial_ota`; normal RHO service tests
retain the previous 8 mm entry.

Startup entry commands 1 mm inward, then 6 mm outward. Assuming commanded
free travel occurs, entry can command at most 1 mm into the inner stop or
5 mm into the outer stop. From a known start S, the test guard uses
`min(425, max(0, S - 1) + 6)` as remaining inward distance. Production does
not receive S. It searches for the first SG trigger with a full-travel pulse
cap, then allows up to 2 mm new inward progress per retry and 4 mm beyond
that candidate in total. Reserve 1 mm for the entry probe. A distant false
candidate can cause a safe failure instead of authorizing another full-travel
search through the stop.

Keep the dedicated 500 mA / 12 mm/s / u8 interpolated profile, 85% five-of-nine
detector, 6 mm backoff, 450 ms gate, and 0.4 mm three-contact span. Startup
coarse arming uses 5.6 mm, matching the tested short-return gate. The entry
can reach the outer stop, so startup does not require the outward leg's SG
load-recovery test before beginning the inward search. UART validation and
pulse caps still apply.

A separate watchdog aborts (never accepts home) after a full 32-sample fresh
window with at least 24 SG values below 200, or eight below 100. This protects
against starting with an already-stalled baseline; qualify these absolute
thresholds only for the fixed homing profile. The whole cycle has a 90-second
deadline. These limits do not guarantee physical overrun when SG detection or
free-motion assumptions fail. The user has accepted SG-based startup with
that limitation; the website abort remains a backup.

| Startup trial UTC | Known start | Contacts / span | Physical review |
|---|---|---|---|
| 04:58:27 | 0 mm | 3 / 0.285 mm | Camera aligned; API confirmed |
| 04:59:45 | 10 mm | 3 / 0.1125 mm | Camera aligned; API confirmed |
| 05:02:36 | 100 mm | 3 / 0.1575 mm | Camera aligned; API confirmed |

These are initial startup-entry tests, not completed qualification. The first
trial clamped its final backoff to 5.78 mm after an early candidate; its final
return still agreed. Preserve the full traces under `tuning-recordings/rho-startup-20260915`.
During review, the independent known-position guard could shorten that return
by the first contact's commanded overrun. Remove that influence for subsequent
startup trials: the candidate ledger determines the return length; the test
guard retains only its inward cap and final physical-reference veto. Repeat
the frozen qualification batch after this change. The three rows above are
entry screening, not the final qualification set.

The first candidate-only-backoff trial started at a commanded 425 mm
(05:13:20). Its three contacts spanned 0.1325 mm, but ended 0.835 mm
short of the nominal home coordinate. The independent guard rejected it
with failure 4. Camera review matched the established home reference;
the first and third contacts also had microphone rises above 6 dB.
Keep this as a rejected instrumented trial, not a pass. Contact with the
outer stop can invalidate the staged command position, and the nominal
425 mm travel has no independent submillimetre measurement. Do not infer
an exact travel calibration from these SG contacts or widen the 0.4 mm
agreement setting. Repeat interior-start tests without outer contact.

After the next trial-image upload, an active-probe abort test observed
136 of the 400 entry pulses before requesting abort. The HTTP reply arrived
in 65.5 ms; the final count was 231, the pulse timer stopped, both RHO
drivers read back `TOFF=0`, and the state remained `INITIALIZED`. The camera
matched home. This checks cancellation during the pulse-capped inward
probe, not cruise stopping distance or operation with failed UART.
The abort endpoint now also invalidates an already-IDLE home, and latches
cancellation of a pending automatic boot home. Explicit later homing remains
available. Manual and tuning page buttons use this dedicated endpoint.

Frozen candidate-only-backoff checks:

| Trial UTC | Start | Contacts / final span | Review |
|---|---|---|---|
| 05:18:31 | 0 mm | 4 / 0.2225 mm | Instrument checks passed; camera home; API confirmed |

The zero-start check rejected its first three-contact cluster and accepted
the last three. Its deepest command coordinate was 0.8525 mm beyond the
known post-entry reference, in addition to the separately capped entry probe.

### Website abort and SG-startup authorization

The user approved qualifying SG-based unknown-position startup with automatic
travel/time limits, accepting that missed detection cannot guarantee a 5 mm
physical-overrun cap. This resolves the safety-choice question from the prior
session; it does not qualify the startup algorithm or enable power-on motion.

The main page now offers **Abort homing** without a confirmation dialog or a
dependency on the first status poll. `POST /api/home/abort` uses the emergency
motion-stop path. Manual and tuning pages label their existing stop controls
as homing abort controls too. Abort also invalidates `HOMING_REVIEW`, covering
a request that arrives after the final contact.

Emergency stop publishes cancellation, stops STEP generation before UART
transactions, and disables both RHO bridges for an active or review-stage
homing cycle. The settings-restoration path checks cancellation before enabling
drivers, preventing a late homing task from restoring power after abort.
The controller leaves the position untrusted. If UART disable verification
fails, it records `ABORT_DISABLE_FAILED`; remove motor power if motion persists.

The main-page button reports missing acknowledgement after 2.5 seconds and
allows retry. That browser timeout does not stop the motor. Keep a physical
power cutoff available: Wi-Fi, the browser, and human reaction time are not
unattended stall safeguards. Do not replace firmware caps with this button.

Validation: production and service builds pass, along with 26 host trace
tests and five JavaScript abort tests (acknowledgement, idle, HTTP failure,
network failure, timeout). A live immediate-cancellation test acknowledged in
77.3 ms, left both RHO drivers at `TOFF=0`, and kept `INITIALIZED` with no STEP
activity. The camera matched home. This was a pre-motion cancellation, not a
measurement of stopping distance during cruise or under failed UART.

After the abort update, the 04:45:15 known-zero homing trial passed with three
contacts spanning 0.25 mm. The review-stage abort then returned the controller
to `INITIALIZED`, and both RHO bridges read back `TOFF=0`. A stale home-confirm
request returned HTTP 409. Camera review matched physical home; leave the
logical position untrusted to preserve the abort outcome. No motor motion
followed this review-stage abort.

### Resumed validation, after 04:09 UTC

The user restored illumination and confirmed that the mechanism was homed.
The lit camera view matched home; the pending 03:55:05 trial was then confirmed
through the API. Its delayed endpoint verification is recorded separately from
the original instrument report, which correctly retains `accepted=false` at
capture time.

Freeze the 6 mm backoff candidate at 500 mA requested, u8 interpolation,
12 mm/s, 85% / five-of-nine SG votes, 450 ms minimum travel, 8 mm runway,
and 0.4 mm three-contact span for the next validation set. Keep normal motion
and the 2 mm per-approach / 5 mm total overrun caps unchanged.

| Validation trial UTC | Known start | Contacts / final span | Endpoint |
|---|---|---|---|
| 04:09:52 | 0 mm | 3 / 0.2175 mm | Camera-confirmed home |
| 04:13:53 | 200 mm | 3 / 0.05 mm | Camera-confirmed home |
| 04:21:35 | 400 mm | 3 / 0.0925 mm | Camera-confirmed home |
| 04:23:09 | 10 mm | 3 / 0.06 mm | Camera-confirmed home |
| 04:24:03 | 25 mm | 3 / 0.09 mm | Camera-aligned with home |
| 04:25:35 | 0 mm | 3 / 0.0425 mm | Camera-confirmed home |
| 04:28:21 | 100 mm | 4 / 0.355 mm | Camera-confirmed home |
| 04:31:01 | 10 mm | 3 / 0.15 mm | Camera-confirmed home |
| 04:32:08 | 25 mm | 3 / 0.23 mm | Camera-confirmed home |
| 04:33:40 | 0 mm | 3 / 0.13 mm | Camera-aligned with home |
| 04:36:22, after power cycle | 0 mm | 4 / 0.24 mm | Camera-confirmed home |

The first ten rows complete the warm validation batch: 24,008 of 24,008 UART
reads were valid. Nine cycles needed three approaches; the second 100 mm cycle
needed four. Its first three command coordinates spanned 0.6325 mm, so firmware
continued and accepted the last three, spanning 0.355 mm. Retain the 0.4 mm
total-span limit; a tighter limit would reject that final cluster. These ten
cycles support repeatability under the tested conditions, not a quantified
field failure rate.

The largest inward command high-water mark in the warm batch was 0.7175 mm
past the known zero, within the 2 mm per-pass and 5 mm cumulative caps. Those
are command-ledger distances, not displacement through the hard stop. All
endpoint camera reviews preceded API confirmation. The original instrument
artifacts retain their capture-time `accepted=false` review state.

The user then performed the requested power cycle without moving the mechanism.
The ESP32 reported power-on reset reason 1, both drivers reported reset, and
the camera still matched home. The first homing cycle after that reset needed
four approaches, with command coordinates +0.3525, +0.9125, +0.6725, and
+0.8575 mm. The final span was 0.24 mm. The host checks passed, and camera
review preceded home confirmation. This is one power-cold known-zero cycle;
it does not test starting cold at an unknown position or a temperature range.

### Cleanup and remaining startup work

Remove the unused two-pass `RhoHomingBounds.hpp` helpers and the stale compile-time
backoff constant. Test the live `RhoRollingSearch` implementation across twelve
retries instead, including rejected over-limit commands and no allowance refund
after backoff. Preserve historical trace replay support and paired-motor safety
code: both still have callers and regression value. Correct the service-build
description and trace pass-number comment without changing live motor settings.

The user requested automatic power-on homing and a merge to main after
qualification, followed by main-only acoustic retuning. Keep the startup flags
off while designing and testing the unknown-origin entry path. Merely enabling
both flags cannot work: `homeAxis()` still requires an independent known-home
coordinate, and successful homing still enters `HOMING_REVIEW`.

Startup qualification must cover both travel extremes and a cold driver:

- Do not issue the present 8 mm outward runway from an arbitrary boot position.
  At the 425 mm outer stop, that command could hit the opposite end stop.
- Do not replace the known-home coordinate with 425 mm and retain the existing
  caps. At physical zero that permits a long inward command if SG misses contact.
- Test the new entry path with an independent commissioning bound that acts
  only as an emergency veto, keeping its known position out of detector decisions.
- Retain failure lockout and verify driver restoration before automatic origin
  assignment. Camera and microphone may validate tests but cannot become boot
  dependencies.

Until that work passes, do not claim unattended startup readiness or begin the
user's follow-on acoustic campaign as though the homing milestone were complete.

The 04:24 trial's three command coordinates were slightly early (-0.0475,
-0.085, -0.1375 mm). The camera cannot resolve that small displacement reliably;
describe its endpoint as visually aligned, not as proof of exact physical
hard-stop contact. The next zero-start cycle also passed. Keep the distinction
between trigger repeatability and absolute mechanical accuracy.

The 400 mm trial had all 10,945 UART reads valid. Coarse contact occurred at
the expected 163,200-step coordinate; subsequent contacts were -0.0125 and
+0.08 mm relative to it. This is a successful known-position near-full-travel
trial, not qualification of an unknown boot position.

The 200 mm trial had 5,641 valid UART reads out of 5,641. Its contact
coordinates were -0.0375, -0.03, and +0.0125 mm relative to the known zero.
For the 100 and 200 mm coarse approaches, excluding the last millimetre,
free-travel SG minima were 230 and 228; medians were 272 and 266. The long
approaches did not reproduce the task-timer's mid-travel false contacts.

After the user repositioned the light, the camera's old high-brightness manual
settings overexposed the scene. Aperture-priority auto exposure, brightness
128, contrast 128, and backlight compensation off restored a clear view.
This changes camera photometry only, not the motor profile or position ledger.

The prior illumination pause below is historical, not the current blocker.
Unknown-origin boot homing remains disabled pending separate entry-sequence
qualification.

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
| 03:48:58 | 25 / 10 mm | 3 / 0.38 mm | Home |
| 03:52:08 | 25 / 6 mm, 450 ms gate | 3 / 0.295 mm | Home |
| 03:52:48 | 0 / 6 mm, 450 ms gate | 3 / 0.2775 mm | Home |
| 03:55:05 | 100 / 6 mm, 450 ms gate | 3 / 0.1825 mm | Unverified: room became dark |

At the 25 mm start, 8/10/15/25 mm backoffs took 8.47/9.13/10.777/14.104
seconds respectively through the last contact (excluding staging and host
idle recording). There is no demonstrated reliability advantage to the longer
backoffs. Six and eight millimetres are the current shortlist, not a proven
optimum. The 6 mm return collected 83..90 SG samples in the first three trials,
above the detector's 57-sample minimum, but with less margin than 8 mm.

### Pause after the 100 mm trial

Seven hardware-timer runs have camera-confirmed home positions; the eighth
(100 mm start) passed the instrument checks but remains `HOMING_REVIEW`.
Its contacts were +0.05, +0.2325, and +0.1825 mm in the inward command ledger,
with all 2,988 UART reads valid. These coordinates are not measured physical
positions. The room became dark before the endpoint camera check. Maximum
camera exposure and a zoom/contrast check did not restore a sufficiently clear
view; do not mark this run physically accepted. No motion followed it.

Restore illumination and check the endpoint before confirming this trial or
resetting the logical origin. Camera controls were restored to brightness 230,
contrast 128, gain 255, manual exposure 875, and zoom 100. The board is stopped
with no queued motion; its logical rho=100 is the stale pre-home value, not
evidence that the mechanism is still 100 mm out. The live test profile remains
6 mm backoff / 450 ms gate, not a promoted production default.

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
`POST /api/tuning/homing`, to select 6..50 mm without reflashing. Start the
comparison with 8, 10, 15, and 25 mm. Firmware clamps each backoff to the
available outward distance from the first inward approach's starting point,
accounting for previously consumed overrun. Start comparison trials at least
25 mm out so the travel clamp does not erase the differences between choices.
After the 8..25 mm screen, test 6 mm with `minimumTravelMs=450`; the old
650 ms gate requires 7.8 mm and is unsuitable for a 6 mm return. Retry arming
still waits until backoff minus 0.4 mm. Do not shorten to 4 mm: at 12 mm/s the
500 ms ramp takes roughly 3 mm, and the detector then needs 57 fresh samples
(48 baseline plus nine decision samples), about 2 mm at observed UART cadence.
If 6 mm lacks margin, bisect upward to 7 mm or retain the 8 mm candidate.

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
  --minimum-travel-ms 650 --backoff-mm 8
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

## Earlier threshold-search guidance (superseded by the current experiment)

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

Screen backoff choices first, then freeze the selected timer, current, speed,
SG filter, and agreement window. Perform at least ten additional complete
cycles at that fixed profile across starts of 0, 10, 25, 100, 200, and 400 mm,
including power-cold and warm driver starts. Screening successes at different
backoffs do not count as ten repetitions of the selected profile. Require:

- zero false triggers and zero missed contacts;
- all UART samples valid except isolated retries, never three consecutive
  failures;
- main-RHO coarse and precision terminal commands within the 2 mm
  known-origin window, and the final camera image aligned with zero;
- no approach extends the inward high-water mark by more than 2 mm, and no
  cycle extends it by more than 5 mm total;
- coarse arming only after 7 mm of inward runway travel and three consecutive
  contacts within a 0.4 mm total span (provisional until validation);
- clear separation between normal-load and terminal SG distributions;
- backoff SG recovery on every cycle;
- no driver, thermal, short, undervoltage, or planner fault;
- exact readback restoration of the incoming acoustic settings;
- the post-home acoustic regression still passes that profile's threshold.

Keep unknown-origin boot homing disabled until its own entry sequence is
qualified. A known-position trial's outward runway and independent home veto
are not available from an arbitrary boot position: 425 mm is a maximum-travel
label, not a measurement of remaining distance. In particular, an outward
runway at the outer limit and a long inward search starting at the inner stop
need separate handling. Repeated agreement cannot provide an independent
2..5 mm physical overrun guarantee after position is lost.

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
