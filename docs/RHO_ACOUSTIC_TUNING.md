# Rho commissioning and acoustic tuning playbook

Use this playbook to select quiet, reliable settings for the configured rho motors.
It uses the same Antlion USB microphone and
the telemetry-aligned analysis in `scripts/acoustic_tuner.py`.

## Current assembled main-only retune (2026-09-15)

### Operator-adopted default, 2026-09-16

The operator ended acoustic tuning for now and selected **5.5 mm/s** as the
default. The microphone is no longer available. Adopt the complete short-screen
candidate: acceleration 20 mm/s², jerk 100 mm/s³, run/hold requests 350 mA,
u8/interpolation, StealthChop, VSENSE high sensitivity, F2/TBL0/REG1/LIM8,
automatic current on, automatic gradient off, PWM_OFS128/GRAD0, CoolStep on
with SEMIN2/SEMAX1/SEUP2/SEDN0/TCOOLTHRS1000. The firmware fallback defaults
now match this selection; the device's saved settings already match it.

This is an operator acceptance decision, not a new acoustic qualification.
Retain the earlier failures and inconclusive measurements. Full-travel/stress
sound qualification and the quieter tiers remain incomplete. Keep theta's
0.225 rad/s profile, CW disabled and the dedicated homing parameters unchanged.
Do not run more microphone tests as part of deployment.

Deployment: uploaded regular `esp32dev_ota` firmware from source commit
`e0545f7`, restoring the previously requested automatic startup homing. Readback
confirmed 5.5 mm/s, the selected driver profile, theta unchanged and CW TOFF=0.
The firmware completed startup homing (cycle 1, no failure, 307/307 valid UART
samples) and reported IDLE at rho=0. No camera or microphone confirmed physical
position. The SD mount still fails, so pattern storage is unavailable.
The release build, 102 host tests and native motion tests passed. See
`docs/RHO_ADOPTED_PROFILE_DEPLOYMENT.json` for the binary hash and readback.

### Speed-first search, 2026-09-16

Latest continuation, 17:05 UTC: the 5.5 mm/s recheck and a new 5.75 mm/s
comparison each completed two 25 mm out/back repeats with healthy motion
checks and commanded zero return. Neither recording supports a tier decision.
The 5.5 recheck had 3.59 dB global background drift in repeat 1, no confirmed
motion-locked tones, and a -48.34 dBFS outward incremental bound in repeat 2.
At 5.75 both repeats failed local-background stability; conditional incremental
upper bounds were -58.58/-48.12. These results neither disqualify the motor
nor improve the earlier quiet 5.5 mm/s short-screen result.

Separate motor-correlated evidence from raw total sound. FFT bins do not
identify sources. Adjacent-idle subtraction estimates the incremental sustained
median under the assumption that those idles represent the background during
motion. It excludes energized idle noise and can miss sparse bursts. A clipped
result such as -200 dBFS is not proof of silence. Keep uncertain transients
unresolved and retain the existing whole-motion qualification guard; a raw
exceedance alone does not prove the motor exceeded its target.

A stationary hold-current A/B/A probe used 350/100/350 mA requests while
keeping run current at 350. Settled CS readbacks were 10/2/10; STEP epoch
remained 20 and commanded position remained zero. Across three 15-second
recordings, window medians were -53.72/-53.35/-54.46 dBFS, with p95 values
-46.63/-48.30/-40.76. The lower-current recording did not show a reversible
median reduction. Changing stationary sound still masks the comparison; this
does not prove that all of it comes from the room or that hold current has no
acoustic effect. Restore hold current to 350 and the provisional speed to 5.5.

The three earlier post-reset 4 mm/s trials inherited PWM_GRAD=2 (their result
JSON records it). The new 5.5/5.75 comparisons explicitly set PWM_GRAD=0 to
match the earlier quiet candidate. Do not describe those post-reset settings
as identical to the pre-reset profile. No camera was used in this continuation.
No final -60/-63/-66 profile, new 400 mm traversal or stress qualification is
complete. The board remains stopped in commissioning at commanded zero, with
CW disabled, theta unchanged and service-image boot homing off. Preserve the
earlier panic as unresolved; it has not recurred since the user's power cycle.

**No final acoustic profile is qualified.** At 02:29 UTC an unexpected ESP32
panic reset interrupted preflight before the 4 mm/s capture. The trial failed closed
in preflight with no motion commands issued. API `resetReason=4` maps to
`ESP_RST_PANIC` in the installed ESP-IDF headers. The board now reports
INITIALIZED, manual service mode and the unknown-start placeholder rho=212.5,
not a measured physical position. CW remains TOFF=0; boot homing remains off
in the installed service image. Do not resume the sweep by merely assigning
zero. Camera `/tmp/rho-unexpected-reset-home.jpg` matches the saved home
reference, but does not diagnose the crash. No ESP32 USB serial device is
connected, the error API is empty after reboot, and `partitions.csv` has no
coredump partition. A serial panic/backtrace would help identify the cause.
The failed-trial JSON and host log preserve the preflight failure; no WAV was
recorded for that attempt. Root cause remains unknown.

The operator then power-cycled the board, requested continued testing and
confirmed the mechanism was at home. Readback showed resetReason=1, stopped
motion and CW TOFF=0. We established commissioning zero from that new physical
confirmation. The operator prohibited further camera use and turned off the
lights. Do not capture more camera images; stop if another reset or uncertain
position invalidates this reference.

The resumed 4 mm/s / 350 mA CoolStep run completed both 25 mm out/back
repeats with healthy driver/planner checks. Both acoustic comparisons failed
background validation (outbound idle drift 7.70/5.60 dB). The 150 mA fixed-current
comparison also completed its motions but had unstable backgrounds, with an
idle upper bound of -48.37 dBFS. Neither test ranks motor noise. Pause motion
and measure stationary background before spending more trials in this noise.
The attempted 150 mA repeat remained contaminated. A 20-second stationary
recording at 04:56 UTC illustrates why the old baseline aggregate is unsafe
as a readiness check: its median-per-frequency spectrum gave -64.99 dBFS,
while time-window median/p95/max were -59.82/-47.41/-43.55 dBFS. About 51%
of the windows exceeded -60. The changing spectra suggest external noise but
do not rule out energized-driver or supply noise. At 04:59 UTC another idle
recording gave median/p95/max -56.81/-48.12/-43.92, without clipping.
After a 25-second quiet interval, the 05:00 UTC 20-second recording still
gave median/p95/max -61.86/-48.50/-42.68. Leave motion stopped until the
stationary sound permits comparison. No further reset occurred during the
three resumed trials; they returned to commanded zero. This is a command
ledger, not a physical-position measurement. The last applied trial settings
are 4 mm/s, 150 mA run/hold request, CoolStep off, u8/interpolation,
F2/TBL0/REG1, acceleration 20 and jerk 100; do not promote them as a final tune.

The host `baseline` command now reports `stationaryBackground`: A-weighted
power quantiles across 85.3 ms FFT windows at 21.3 ms cadence (48 kHz), mean
window power and clipping fraction. It retains the old aggregate with an
explicit warning. Use the window statistics to decide whether another trial
is worth running; they do not attribute sound to a source or qualify a tier.
Motion-trial acceptance criteria remain unchanged. Three regression tests
cover a steady tone, intermittent noise and invalid input; 99 host tests pass.

The operator requested that we optimize settings at **6 mm/s first**, measure
the lowest repeatable noise at that speed, then decrease speed in small steps
to find the quieter tiers. Start with 0.5 mm/s steps; refine crossings with
0.1-0.25 mm/s steps. Recheck neighboring driver settings at each crossing:
mechanical resonances and driver behavior need not vary monotonically with
speed. Use binary refinement only inside a measured monotonic bracket.

Keep the full physical 0..425 mm range, including 0..1 mm. The operator
withdrew the proposed clearance after observing another zero-return test.
No firmware clearance or location-dependent slowdown has been implemented.
The 01:10 UTC three-repeat 4 mm/s test returned to commanded zero with healthy
planner and driver checks, but changing background invalidated its acoustic
rating. Do not use an isolated peak to identify motor noise or dismiss it as
room noise without repetition and timing evidence.

At 6 mm/s, start with zero-inclusive 0 -> 25 -> 0 mm screens at 200 mA,
u8/interpolation, StealthChop, automatic current on, automatic gradient off,
PWM frequency 2, regulation 4, limit 8, acceleration 20 and jerk 100.
Compare the existing quiet candidates one parameter family at a time, with
interleaved baseline repeats. Include frequency 3, current, PWM regulation
and blank time; revisit microsteps and PWM adaptation only at a verified
physical zero. Preserve the 500 mA motor rating and existing CS14 normal-motion
cap. Keep CW disabled and theta stationary. Treat SG collapse, UART faults,
planner faults or uncertain position as safety stops, not acoustic failures.

Rank candidates by valid sustained-motion upper bounds, with energized idle,
whole-STEP transients and motion-locked tones reported alongside them. Retest
contaminated or inconclusive comparisons. A short `verify` run cannot qualify
a sound tier. Confirm finalists with repeated gated trials, 0 -> 400 -> 0 mm
travel and stress tests, then restore and verify production boot homing.
Report measured dBFS rather than promising a global optimum or a tier below
the available measurement floor. Homing uses its separate frozen profile.

Early valid 6 mm/s measurements at 150 mA / u8 / frequency 2 / TBL 1 /
regulation 4 gave sustained incremental upper bounds of -67.10 and -66.76
dBFS across two 25 mm out/back repeats. Raw cruise upper bounds were -63.12 /
-62.33 and -62.92 / -62.48 dBFS, with local idle drift <=0.31 dB. The 200 mA
interleaved comparison was only 0.3-1.3 dB louder in incremental bounds; retain
150 mA as a provisional candidate rather than a decisive current optimum.

Both currents exceeded -60 dBFS on return transients. Independent waveform,
position and timing review found bursts near 21.7, 17.7 and 13.7 mm. The 200 mA
13.7 mm return event repeated at 13.69/13.68 mm. The roughly 4 mm spacing
suggests periodic motor/mechanical excitation; it does not identify the part
responsible. Recorded >100 us timing outliers did not overlap those bursts.
The -28.43 dBFS outbound maximum in the first 200 mA comparison preceded the
first STEP by about 92 ms and coincided with loud adjacent idle. Preserve it
as a contaminated edge window, not evidence of a loud motor onset.

Regulation 1 at 150 mA retained stable sustained bounds (-67.32/-67.61), but
return peaks remained -59.08/-56.96 dBFS. Shorter blanking (TBL 0, regulation
4) gave one stable screen at -67.98 incremental and -63.16 raw cruise upper
bounds, with a -57.12 return peak. These short screens have not qualified a
tier. Evidence is under `speed-first-6/` and in
`docs/RHO_SOUND_SPEED_FIRST_RESULTS.json`. Initial screens use 0..25 mm;
the final short confirmation extends to 0..50 mm.

The 350 mA request became the strongest fixed-current candidate. One screen
measured -62.52 dBFS whole-STEP maxima in both directions. A 275 mA comparison
had stronger roughly 4 mm-spaced return bursts despite near-matched idle;
the independent spectral/timing audit supports a transient improvement beyond
the quieter background. Sustained incremental bounds remained near -67.7.
400 and 450 mA requests did not improve the screen. **325 and 350 mA both
read back CS=10**, about 337 mA nominal with the configured 0.11-ohm shunts;
treat those as repeats of one current code, not distinct current settings.
These are calculated currents, not measured coil RMS current.

Do not promote the best fixed-current screen to a qualified profile. Its
later confirmation included a -54.16 dBFS return burst near commanded
18.1..17.5 mm, with quiet adjacent idle and no coincident recorded STEP timing
outlier. A second repeat stayed near -61.57. The burst overlaps an earlier
problem region but has a different spectrum. Attribution remains unresolved;
retain the failed/inconclusive repeat. Neither 4 nor 16 external microsteps,
frequency 3, nor longer blanking improved on the strongest u8/F2/TBL0 screen.

Regulation 1 gave two consistent fixed-current screens, with whole-motion
maxima near -59.74/-59.98 dBFS and return peaks near -61.2. Regulation 2 and
8 did not show a clear improvement. CoolStep now has a useful provisional
candidate: run/hold request 350 mA, u8/interpolation, F2/TBL0/REG1/LIM8,
automatic current on, automatic gradient off, SEMIN=2, SEMAX=1, SEUP=2,
SEDN=0, TCOOLTHRS=1000. Readback confirmed actual current codes 5..10 during
motion. Its first screen peaked at -60.83/-61.38 dBFS. SEMIN=3 held code 10
and did not improve noise; SEUP=0 gave similar maxima (-61.54/-60.70).
Keep IRUN >=10 for these half-current CoolStep trials, per the datasheet.
Do not reuse CoolStep below that current-code range without revisiting its
operating assumptions. Homing continues to force CoolStep off.

The gentler 10/50 acceleration/jerk screen peaked at -61.28/-60.39; the faster
40/200 screen at -59.46/-60.01. Retain 20/100 for the descending-speed search.
The three longer 0 -> 50 -> 0 mm confirmations failed the transient screen:
whole-STEP outward/return maxima were -60.82/-59.62, -57.74/-59.59 and
-58.11/-45.40 dBFS. Sustained incremental upper bounds were -67.41, -65.32
and -66.52; quiet sustained sound does not qualify these whole movements.

The last burst peaked near commanded 16.05 mm, about 2.92 seconds before
STEP stopped, with 621/1242 Hz harmonics. Adjacent driver polls showed SG
194..238 and current code 5, no fault or current transition, and no coincident
recorded STEP timing outlier. This does not support sustained endstop contact;
it also does not prove an ambient source or exclude brief slipping. Motion
was paused and the camera matched the saved home reference before continuing.
The camera is not an encoder. Preserve this as failed/inconclusive evidence.
No final tier, 400 mm range test, or stress qualification has passed in this
new search yet. Start the next bracket at 5.5 mm/s without weakening the
transient or background checks.

Descending short-screen results with that CoolStep candidate:

| Speed | Loudest whole-STEP window | Sustained incremental upper bound | Outcome |
| --- | --- | --- | --- |
| 5.5 mm/s | -60.83 dBFS | -67.86 dBFS | Two 25 mm out/back repeats pass the -60 screen only |
| 5.0 mm/s | -57.62 dBFS | -65.11 dBFS | Repeated return burst near 13 mm; fails -60 screen |
| 4.5 mm/s | -57.59 dBFS | -62.15 dBFS | Both repeats too loud for -60 screen |
| 4.0 mm/s | Not measured | Not measured | Board panic reset before capture/motion |

At 5.5 mm/s all legs sustained target velocity for about 4.25 seconds;
background, gain and timing checks passed. The margin is only 0.83 dB and
`verify` is not qualification-eligible. At 5 mm/s the return burst repeated
near 12.7..13.0 mm with roughly 1.54 kHz ringing; an outbound ~246 Hz tone
also repeated. CoolStep readbacks changed from mostly code 9 at 5.5 mm/s to
code 10 throughout 5 mm/s cruise, compared with code 5 in the 6 mm/s long
confirmation. These are not constant-current speed comparisons. Refine near
5.5 mm/s and recheck current settings at slower speeds after the reset cause
is addressed; do not apply a monotonic binary search across this resonance.

### Endpoint diagnosis, 2026-09-16

The 200 mA / u8 / frequency 2 / TBL 1 / PWM_REG 4 profile at 2 mm/s
completed the diagnostic path 0 -> 1 -> 11 -> 1 mm. Whole-STEP short-window
maxima were -65.42 / -64.67 / -64.97 dBFS. No FFT frame exceeded -60 dBFS.
The driver emitted 8,400 pulses, matching 21 mm at 400 steps/mm. Timing and
driver checks passed. This isolates a quiet interior window; it does not
qualify travel down to zero. The 1 mm setup leg also lacks enough sustained
cruise for the aggregate qualification check. SG reached zero during the quiet
final deceleration, so that low-speed sample alone does not establish contact.

A 4 mm/s follow-up over 1..51 mm had raw cruise upper bounds of -64.46 and
-64.73 dBFS, but an outbound short-window peak of -53.77 dBFS and a return
peak of -60.23 dBFS. The outbound peak occurred during interior travel, so
endpoint clearance alone does not yet qualify faster motion. Keep this run
unqualified pending attribution and repetition; do not substitute its quiet
cruise medians for whole-motion sound.
The independent review located the main burst around 14.6..17.6 mm outward;
the return through those positions did not reproduce it. It also found 32 new
STEP timing outliers over 40,400 pulses, all outward (847 us maximum lateness,
804 us interval error, 911 us maximum callback gap). One retained outlier
overlapped the burst; most preceded it. There were no queue underruns.
Dispatch disturbance, direction-dependent loading and room noise remain
competing explanations. The latest-only outlier stream cannot establish full
event-by-event attribution. Investigate this before increasing speed further.

After a bounded homing audit, raising normal requested current to 450 mA
(within the existing CS14 cap) made the zero-return burst worse: -43.21 dBFS
over the short whole-STEP window, compared with earlier -49 to -50 dBFS at
200 mA. The peak occurred near the final zero endpoint, 13.77 dB above the
louder adjacent-idle maximum. Room/hold noise changed during this trial;
do not use its cruise medians as a clean current A/B comparison. Planner and
driver checks passed, but this profile is rejected for quiet motion.

I proposed a 1 mm normal-motion clearance to the operator. The operator later
withdrew that choice; no production minimum radius, zero, or homing parameter changed.
Further offset tests are diagnostics only. A final quiet profile must state
its tested range and pass repeated range, acceleration and stress checks.
Boot homing remains OFF in the installed service image during this work.

The host now recognizes `HOMING_FAILED` as stopped only after checking an
inactive timer, planner and STEP epoch plus an empty queue. It preserves the
failed state and coordinates. Homing cleanup attempts both stop and recorder
shutdown without hiding the original exception. The acoustic
`--rho-leave-at-window` option permits one offset `verify` test, retains the
absolute coordinate ledger and forbids automatic-gradient preconditioning.
It leaves the carriage at the specified window start. Use the separate known
position fields for a subsequent near-home audit, e.g. `--rho-start-mm 1
--companion-start-mm 0`; the generic known-start CLI requires zero or >=10 mm.

The first clearance artifact mislabeled its +1 mm finish as a return to its
starting zero. Keep the original and its reporting erratum together. The
host now reports `returnError` relative to the start and `expectedFinalError`
relative to the intended endpoint. Neither clearance trial can qualify a
production profile. Evidence: `clearance-diagnostic/`, `endpoint-current/`,
and the 00:56 / 00:58 endpoint audits. All 96 host tests pass.

The final 01:00 UTC endpoint audit passed with contact ledger positions
2415 / 2399 / 2470 steps (0.1775 mm span). Audio rises were only 1.3-1.6 dB;
the camera agreed with the saved home reference, and I accepted the completed
homing result through `/api/home/confirm`. The board is stopped at rho 0,
with requested run/hold current 200 mA and normal speed restored to 2 mm/s,
acceleration 20 mm/s², jerk 100 mm/s³. These are provisional settings, not a
qualified zero-inclusive sound profile. CW remains disabled; theta is unchanged.

**Position recovered on 2026-09-16 at 00:43 UTC.** The operator reported a
small outward offset and turned on the light. I ran the existing short-bound
Home route without changing the homing settings or assigning logical zero.
The first attempt reached its 3,200-pulse approach cap (8 mm) before a valid
contact vote, reported failure 2 and disabled the RHO bridge. Preserve this
failed attempt alongside the earlier 35 passing audits.

The longer-exposure camera image then placed the carriage near the saved
home reference. A known-position test request received HTTP 409 because the
stale logical position disagreed with its requested start; that request caused
no motion. I used the existing bounded Home route for the verification cycle.
It returned three contact coordinates of 2494 / 2476 / 2540 steps, a 0.16 mm
span, with 298/298 valid UART samples. CW stayed disabled and the driver
settings matched their pre-home values. The final image agreed with the saved
home reference at camera resolution. I accepted that completed homing result
through `/api/home/confirm`; the board now reports `IDLE`, rho 0, STEP inactive.
The camera did not measure the 0.16 mm span. Evidence is in
`near-home-recovery/results.jsonl`; no quiet-motion trial followed recovery.

The installed service image still has boot motion OFF and does not expose the
production unknown-position homing path. Its short-origin cap can truncate a
recovery from an uncertain outward position. Do not describe that service-mode
restriction as a camera dependency of production homing. Before further sound
automation, address the host stop helper's failure to recognize a stopped
`HOMING_FAILED` state; it masked the HTTP 409 with a 30-second stop-confirmation
timeout. Firmware telemetry showed an inactive timer and empty queue throughout.
The 1 mm/s sound profile remains rejected; no new sound tier is qualified.

**Earlier safety stop, 2026-09-15 at 09:39 UTC:** Three
consecutive SG_RESULT <=5 samples during the return caused an automatic stop.
The retained emitted-step ledger was 2.5125 mm, without a verified physical
position. The camera image was too dark to resolve the mechanism. I paused
hardware testing without assigning zero or increasing the recovery allowance.

Qualification correction: energized idle was not always room background. A
stationary A/B/A check at regulated 275 mA, unchanged mic gain and no STEP
epochs found:

| PWM frequency setting | Stationary A-weighted level | Distinct tone |
|---|---:|---|
| 0 | -56.14 dBFS | 11,718.8 Hz |
| 1 | -56.73 dBFS | 17,543 Hz |
| 2, three interleaved checks | -68.96 / -68.91 / -68.83 dBFS | neither tone |
| 3 | -68.87 dBFS | neither tone |

These are total stationary recordings, not background-subtracted motor levels.
The reversible, settings-locked difference identifies driver-related hold noise;
do not call it environmental noise. Earlier frequency-0/1 motion-excess passes
are not total-sound qualifications. Preserve those results as incremental-motion
comparisons only. Frequency 2's 2 mm/s offset verification measured -60.47 dBFS
motion-excess upper bound with healthy motion, but still needs the corrected
total-sound checks and repeated full-range qualification.

Keep all existing timing, adjacent-idle, gain, clipping, cruise and hardware
checks. Add raw recorded-power upper bounds for each motion gate and every
adjacent idle block. Both must be under the RHO tier as well as the existing
motion-excess bound. If raw sound exceeds the tier but incremental motion passes,
the result is inconclusive: the recording alone cannot distinguish external room
noise from idle motor noise. Missing raw bounds also prevent qualification.
Confidence bounds describe individual windows, not simultaneous 95% coverage
for an entire multi-window profile. Use raw p95 bounds for ramp/stress transients.

Offline replay preserved every original incremental metric and found these
worst raw cruise bounds at 275 mA / 2 mm/s: frequency 0 = -54.55, 1 = -55.35,
2 = -59.67 and 3 = -58.15 dBFS. Frequency 2 is the best of these comparisons
but misses the conservative total -60 ceiling by 0.33 dB. None is qualified.
The replay is in `total-sound-review/results.jsonl`; the reversible idle check
is in `idle-frequency-ab/results.jsonl`. Raw-total exceedance alone is not
proof of motor noise; attribution of the frequency-0/1 hold tones comes from
the separate stationary A/B experiment.

At frequency 2, reducing requested current from 275 to 200 mA did not improve
the offset screen: -59.65 dBFS incremental upper bound and -59.01 raw-total
upper bound, versus the 275 mA replay's -60.47 / -59.67. Both measurements
were valid and motion healthy. Test shorter blanking and regulator response
before lowering current again: section 6.3.1 of the datasheet explains that
blanking time and PWM frequency impose a motor-specific low-current limit.
The motor winding resistance has not been supplied, so the exact lower limit
cannot be calculated from the 0.11-ohm sense shunts.

At 200 mA, reducing TBL from 2 to 1 improved the offset screen to -60.49 dBFS
incremental and -59.70 raw-total bounds. Motion remained healthy, but the
total-sound ceiling still was not met. This is a tentative improvement, not
a qualified profile or proof that the requested current equals measured current.

With TBL=1, reducing PWM_REG from 15 to 4 measured -60.75 dBFS incremental
and -59.88 raw-total upper bounds at 2 mm/s. The 0.18 dB raw change is small;
do not claim a repeatable improvement without confirmation. All motion checks
passed. The first 22 bounded endpoint audits in this acoustic session all
passed (one needed a fourth approach); those are SG-based origin resets, not
22 independent camera confirmations or a quantified field-failure rate.

Long-test timing: the 5,950 mm stress sequence is one STEP epoch, so at
2 mm/s it lasts longer than half of the ESP32 microsecond timer's wrap period.
The host now unwraps successive clock samples and anchors each event locally;
it does not extrapolate an old start from a lower-latency sample many minutes
later. Boundary refinement is limited to 0.5 seconds. Resets, ambiguous gaps,
partial clock fields and epoch regressions fail closed. The sparse-gap check
also uses microseconds: this firmware's `millis` telemetry is derived from the
wrapping microsecond snapshot, not an independent 32-bit millisecond counter.
71 Python tests and the acoustic self-test pass, including 49.6/83.3-minute
epochs, clock drift, rollover-zero timestamps and stale-epoch filtering. A real
four-leg replay retained valid timing with changes within measured RTT bounds.

At 200 mA / frequency 2 / TBL 1 / REG 4 / 2 mm/s, switching external
microsteps from 8 to 2 (interpolation still on) reduced the worst sustained
raw bound to -63.21 dBFS and incremental bound to -65.08. All four gates had
full cruise and clean motion/timing. However, the raw per-gate p95 bounds were
-47.60, -47.09, -48.28 and -49.29 dBFS. Visible bursts occur within the motion
windows. The quiet median is therefore not a whole-motion qualification;
investigate these transients before selecting this profile or increasing speed.

The u4 comparison was worse: -55.80 dBFS incremental and -55.45 raw cruise
upper bounds, with healthy planner/driver readbacks and logical return. The
subsequent bounded endpoint audit passed at contact coordinates 2440, 2481
and 2503 steps (0.1575 mm total span), with 14.43/13.25/6.34 dB audio rises.

Parallel review found that 6.37%, 6.67% and 10.84% of cruise frames exceeded
-55 dBFS for u8/REG15, u8/REG4 and u2/REG4, respectively; none of their idle
frames did. Removing the first and last second of each u2 gate left similar
burst fractions. The strongest u2 bursts last about 55 ms (median waveform
energy duration), with about 90% of excess energy at 1-5 kHz. Quiet cruise
medians cannot qualify these profiles as inaudible.

Burst timing correlates with diagnostic polling more strongly than shifted
controls, but does not yet identify UART interference, microphone EMI, Wi-Fi
scheduling or STEP timing as the cause. Normal motion uses a 50 us
`ESP_TIMER_TASK` callback, one queue event per callback. Delayed execution can
produce late and compressed pulses without a queue underrun. Reported speed
is the planned S-curve, not measured pulse spacing. Homing uses its separate
hardware ISR and is unchanged. First run the read-only stationary
`scripts/acoustic_polling_probe.py` telemetry/UART A/B/A diagnostic, then
instrument actual normal STEP timing before changing the timer architecture.
Retain all bounds and health monitoring during causal comparisons.

Host guard correction: older `verify` trials checked faults and active/CW
bridge state, but labeled every moving driver sample `during-motion`. This
skipped the guard's sustained-cruise SG-collapse check. The quick profile now
labels samples at >=90% target RHO speed `during-cruise`, at the same polling
cadence. This does not retroactively qualify old trials. Driver samples now
record request start and completion times, not only completion, for attribution.

The stationary polling probe completed five 20-second blocks with unchanged
coordinates, STEP epoch 102 and microphone gain. Telemetry-only blocks measured
-63.60/-63.67/-63.53 dBFS median; telemetry plus fast driver polls measured
-63.67/-63.67. No frame exceeded -55 dBFS in any block. Thus polling did not
reproduce the motion bursts at standstill in this test; it does not rule out
motion-dependent electrical interference. Numeric evidence is in
`stationary-polling/20260915T084723Z-stationary-polling.json`. The script issues
only GET requests and checks state, coordinates, STEP epochs and driver health.
All 25 bounded endpoint audits through 08:44 UTC passed on the frozen profile.

Timing instrumentation adds `stepTiming` to motion telemetry: boot-cumulative
per-axis pulse/outlier counts, maximum lateness and interval error, and the
latest outlier's scheduled/pre-rise timestamps and planned/actual interval.
Callback gaps are tracked separately. Require the snapshot's `valid` flag;
counter changes can reveal multiple outliers between polls, but only the latest
record is retained. The 100 us firmware threshold is diagnostic, not a validated
stall or acoustic limit. First pulses after an epoch change have no interval
comparison. Unsigned deltas handle microsecond rollover.

The timestamp is taken after DIR setup and immediately before GPIO writes;
bookkeeping runs after STEP falls. These are software observations, not a logic
analyzer or mechanical encoder. Instrumentation has small unmeasured overhead
and changes telemetry payload size, so compare measured behavior rather than
assuming a perfectly passive observer. Fast driver snapshots now include
`readStartMicros`/`readEndMicros` for the checked UART burst on the same clock.
No timer scheduling, driver settings or homing logic changed. A native injected
10 ms callback delay produces timing outliers with zero underruns and the same
200 commanded pulses, demonstrating why underrun counts alone are insufficient.
[Espressif's timer documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/system/esp_timer.html)
describes task-dispatch delays and overdue callback handling.

The instrumented same-settings u2 repeat confirmed severe normal STEP timing
distortion: 20,000 pulses, 10,061 timing outliers, 16,552 us maximum lateness
and 7,886 us maximum interval error. Some cruise pulses scheduled about 5 ms
apart were emitted only 37-46 us apart, despite zero underruns. Raw cruise
upper bound was -62.55 dBFS, but transient excess p95 remained -50.19. This is
not a qualified quiet tune. Only 734 latest outliers were retained; 9,327
individual timestamps were overwritten between polls. Of 154 interior sound
bursts, 24.7% occurred near retained >2 ms errors versus 7.7% in shifted
controls. That supports association, not complete causal reconstruction.

A hardware-timer candidate moves normal queue consumption to TG1/T0 at the
same 50 us period, with qualified homing remaining on TG1/T1. Its stop path
revokes normal ownership then pauses under the SDK's callback group lock
before clearing the queue. Pending interrupts are cleared before restart;
STOP uses ISR counter pause, not a task timer API. No driver settings,
geometry or homing detector parameters change. Both normal axes share this
engine; only main RHO is being tested physically in this session.

The initial compiled-code review caught flash-resident `atomic<bool>::load`
and `FastGPIO::write` helpers. Word atomics and forced-inlined GPIO corrected
them. The reviewed call graph, clock reader and literals are IRAM/ROM, with
planner state in internal DRAM. Native assertions, 78 Python checks and both
firmware builds pass. This is still a bench candidate pending short bounded
motion, abort, endpoint and same-settings acoustic comparisons; native tests
do not qualify real ISR timing. Production boot-enabled firmware must be
restored after service-image testing.

The first hardware-timer-only 0 -> 10 -> 0 check completed all 2,000 pulses
with valid timing/driver checks and logical return. Sustained raw upper bound
was -66.88 dBFS, but raw gate p95 bounds remained -54.67/-47.35: still not
qualified. STEP lateness reached 8,767 us even though callback gaps were at
most 907 us. The queue was almost always full (510-511 entries).

This exposed a second defect: `fillStepQueue()` appended no-step horizon
markers on nearly every busy producer iteration. The ISR consumes one queue
entry per tick, so hundreds of closely timed placeholders obstructed real
steps. Native reproduction with an uninterrupted timer and correctly ordered
timestamps produced up to 118.5 ms STEP lateness, zero underruns and exact
pulse totals. Thus both task dispatch and placeholder saturation needed fixes.

`queueHorizonMarker()` now permits a marker only at least 1,000 us after any
previous queued event. It never edits a published queue slot, handles clock
rollover and timestamp zero explicitly, and retains heartbeat coverage for
slow motion. Six native regressions cover fast producers, producer pauses,
rollover and one-second STEP intervals; all pass with bounded queues, exact
pulse totals and no underruns. Hardware comparison is still pending for this
second fix. The frozen audits before and after the short run both passed;
29 bounded audits have passed through 09:16 UTC, on SG—not independent camera
or encoder—evidence.

The combined hardware-timer / spaced-marker build (`a1dbdfb`) completed its
first 0 -> 10 -> 0 check with all 2,000 pulses, one >100 us STEP outlier,
112 us maximum lateness and 98 us maximum interval error. This compares with
8,767 us lateness in the same-distance hardware-timer-only check. Sustained
raw upper bound was -67.01 dBFS; raw gate p95 bounds were -66.01 / -60.96,
versus -54.67 / -47.35 before marker spacing. Both runs used the same driver
settings and monitoring. The short comparison supports the timing correction;
it does not qualify full-stroke sound, acceleration or mechanical position.
Both surrounding frozen endpoint audits passed, bringing the session total
to 31 through 09:28 UTC. The post-run contacts spanned 0.1925 mm and had
13.93 / 17.97 / 10.45 dB advisory audio rises.

An outward 20 mm command was interrupted at an emitted-step ledger position
of 12.38 mm. Normal STOP acknowledged in 24.09 ms; ten subsequent samples
over 1.8 seconds retained exactly 3,238 cumulative pulses and the same position,
with inactive timer, empty queue and `INITIALIZED` state. CW remained disabled.
The test harness then failed its mistaken expectation that normal STOP also
disables the active bridge: `emergencyStop(false)` deliberately retains hold
current outside homing. The original failed artifact is preserved in
`normal-stop-validation/results.jsonl`; it is evidence of stopped pulses, not
a passing bridge-disable test. Homing abort uses the separate force-disable
path. Recovery must use the retained 12.38 mm ledger, not reassign zero.
That bounded recovery passed at contacts 7031 / 7035 / 7038, a 0.0175 mm
span, with all three advisory contact-audio rises above 8 dB. Its capture-time
review state is preserved; the qualified SG result was subsequently accepted
through the service API. There have now been 32 passing bounded audits.

Further audio review found one short return-end event in the spaced-marker
test: a -48.57 dBFS A-weighted frame peak, 0.214 seconds before the STEP stop.
The outward leg and return interior had no frames above -60. This event did
not coincide with the lone STEP timing outlier. It may be near-home motion
noise, but one occurrence cannot establish its source. More importantly, the
old transient mask clipped the exact STEP interval using an earlier coarse
end estimate and a 150 ms margin, excluding the peak. Existing p95 summaries
are therefore incomplete endpoint evidence; retain them but do not use them
alone to qualify inaudibility.

The new `whole_step_transient` check uses each exact STEP interval, not the
coarse motion envelope. It takes the worst approximately 100 ms mean of
A-weighted FFT-frame powers, including windows whose signal support touches
either boundary plus clock uncertainty. At 48 kHz this is five frames
(106.7 ms of frame cadence, 170.7 ms actual signal support). This is a
conservative engineering screen, not a confidence bound or validated human
audibility test. Adjacent energized idle is reported separately, never
subtracted. Every RHO gate must be present and under the tier; missing data
blocks qualification and raw exceedance remains inconclusive about noise
source. Existing cruise/p95 metrics and theta acceptance are unchanged.
All 85 Python tests and the acoustic self-test pass, including endpoint-mask
clipping and a sparse burst that occupies less than 5% of a long movement.

Offline replay of the short spaced-marker run measures -65.47 dBFS outward
and -49.11 on return using this check. The same-settings 0 -> 50 -> 100 ->
50 -> 0 run measures -65.16 / -64.08 / -64.23 / -50.01. The final-home event
repeats; it is not a general noisy-cruise section anymore. This longer run
completed all 20,000 pulses with three new >100 us timing outliers, maximum
boot lateness still 112 us, and maximum observed queue depth 208. Its frozen
post-home audit passed. Replay data is in `whole-step-review/results.jsonl`.

At 8 external microsteps with interpolation, the next short run had no new
timing outliers across 8,000 pulses, -67.06 dBFS raw cruise upper bound and
-66.43 outward whole-STEP maximum. The return maximum remained -49.59; finer
microstepping alone did not solve the near-home event. Its frozen post-home
audit passed (contacts 2493 / 2513 / 2493, 0.05 mm span). The audit count is
34 through 09:36 UTC; these are still SG-based origin checks.

Reducing acceleration/jerk to 2 mm/s² / 10 mm/s³ at the same 2 mm/s did not
solve it: raw cruise upper bound -66.86 dBFS, return whole-STEP maximum
-49.61. Its post-home audit passed, bringing the count to 35 through 09:38.
Across u2/u8 and both deceleration profiles, loud-event onset was near
commanded 0.35–0.44 mm. Other 10/50/100 mm endpoints were quiet through their
stop-speed transitions. The gentler return had several ringing events spaced
about 0.08 mm apart in the step ledger. This favors contact/compliance or
near-home loading over a generic StealthChop transition, but is not proof of
physical contact or measured lost steps. Homing currently ends on contact
consensus without a final clearance move. Do not silently change its zero or
declare an offset-only sound test a full-range qualification.

The subsequent 1 mm/s trial aborted before completing its return. Current,
driver health and STEP timing did not report a separate fault in the later
stationary snapshot. SG can be unreliable at low speed, but the evidence does
not justify ignoring its guard. The original runner lost in-trial telemetry
on that exception; only its WAV remains. `failed-trial-review/results.jsonl`
preserves the subsequent stopped-state observation, explicitly not reconstructed
motion/audio timing. The corrected host failure path retains future failed
timelines, driver samples, recorder-clock uncertainty, command/stop attempts and
settings references after stop/recorder cleanup. Failed records cannot qualify,
do not trigger an automatic RHO return, and cleanup or disk-write errors cannot
replace the original guard exception. All 92 Python tests and the acoustic
self-test pass, including injected guard, wrong-snapshot, recorder-start,
cleanup and artifact-write failures. This cannot recover the already-lost
09:39 motion trace. When a checked origin is available, a diagnostic 1 -> 11 ->
1 mm comparison would test clearance from the noisy near-zero region without
redefining zero; it would not qualify operation down to 0 mm.

Homing qualification, production boot enable and cleanup merged to main as
`a558c4e`, including the user-confirmed power-cold boot. Only the main RHO motor is connected;
keep the CW driver at `TOFF=0` and theta stationary. Pass
`--expected-rho-motors main` to the acoustic tool. It checks both UART devices
and the unused bridge, but does not require the unused motor to produce SG,
match active-motor interpolation, or complete StealthChop calibration.

Full-stroke qualification invalidated the near-home speed candidates as whole-
mechanism qualifications. At 5 mm/s, the 100 -> 50 mm return section measured
-54.26 and -53.89 dBFS upper bounds in two independent range runs, versus
roughly -60 to -64 elsewhere. Both runs had valid timing/background, full
cruise, logical returns and healthy drivers/planners. Do not promote 5 mm/s
at -60, or assume the 3.5 / 2 mm/s near-home screens qualify the whole stroke.
Retune against this repeatable noisy section, then repeat the range check.
The bounded post-range home audit passed at 2417, 2391 and 2385 steps; its
last contact had a 7.77 dB audio rise. This remains SG-based, not camera proof.

The focused 50-100 mm screen also failed at 3.5 mm/s (-55.15 dBFS upper
bound), so merely promoting the slower near-home tier would be wrong.

Current-setting caveat: with `PWM_AUTOSCALE=0`, IRUN/IHOLD scale PWM voltage;
they do not enforce the nominal coil current. The fixed-128/0, CS6 profile
read back `PWM_SCALE_SUM=28`. Its actual current depends on coil resistance,
supply voltage and back EMF, and has not been measured. Do not treat the
"200 mA" label or CS14 software cap as a measured/enforced current bound in
this mode, and do not increase fixed PWM strength on that assumption. The
next comparison uses regulated current (`PWM_AUTOSCALE=1`) at CS8 / 275 mA
requested, checking standstill adaptation before motion. This does not alter
the dedicated homing profile, which already uses automatic current control.
See the [TMC2209 datasheet, PWMCONF and section 6.4](https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf).

The regulated 275 mA / manual-gradient-0 comparison settled at standstill
(`PWM_SCALE_AUTO=0`, `PWM_OFS_AUTO=117..118`, `PWM_SCALE_SUM=32..33`). Its
3.5 mm/s offset screen remained too loud: -54.97 dBFS upper bound versus
-55.15 for the fixed-PWM comparison. Both inward 100 -> 50 mm legs were
loud, with stable background, full cruise and no planner/driver errors. This
change establishes regulated current for further tests; it is not an acoustic
improvement. The following bounded home audit passed at ledger contacts
2615, 2588 and 2644 steps (0.14 mm span), with a 21.63 dB first-contact
audio rise. Its origin reset was accepted on SG evidence, not camera evidence.

Reducing that regulated profile to 2 mm/s passed the six-leg offset screen at
-60.74 dBFS upper bound (-61.76 estimated motor excess), with valid local
background/timing, full cruise and no faults. This is the first regulated
candidate to pass the known noisy section, not a full-stroke qualification.

The initial sweep rechecked the historical fixed-PWM 128/2, u8 interpolated,
CoolStep-off family at 200 mA requested run/hold. The older acoustic winners used 150/75 mA at 4.25,
2, and 1 mm/s, acceleration 20 and jerk 100, but later reversal stress lost
synchronization at 150 mA requested. These fixed-PWM current labels are nominal
register settings, not enforced coil currents; use the regulated comparison above
for further current exploration.
Those paired-hardware results are starting points, not current qualifications.
Keep the dedicated homing profile independent of motion changes and inspect
the camera after each completed configuration when illuminated. The SG-based
endpoint-audit exception and its limits for this session are recorded below.

The first assembled screen ran at 200 mA, 2 mm/s, acceleration 20 and jerk 100.
Its four 50 mm legs completed at full cruise, returned to logical zero and had
stable adjacent idle windows. The conservative upper bound was -65.36 dBFS;
the loudest estimated motor excess was -68.89 dBFS. This is a screen, not a
two-repeat qualification. The room baseline was -55.72 dBFS and the microphone
remained at 100% / 0.00 dB.

The light was off after the power-cycle confirmation, so the camera could not
verify the first acoustic endpoint. The subsequent bounded, unchanged homing
sequence agreed at 2460, 2424 and 2491 steps against a 2400-step reference;
two contacts had microphone rises above 16 dB. We accepted that SG-based origin
reset using the qualified homing method. Do not label it an independent camera
check or proof of lossless acoustic motion. The original trial artifact retains
its capture-time `HOMING_REVIEW` state. Stop if the bounded audit fails, and do
not enlarge its limits to recover an uncertain position.

Use the startup-trial service build for this session so endpoint audits exercise
the frozen production entry. Service boot motion remains off during acoustic
testing; restore the boot-enabled production image afterward. A main-only SG
watcher checked the latter portion of the first screen without a sustained
collapse. Subsequent screens also poll driver health and SG during cruise,
including verification that the active bridge stays enabled. Continue checking
that the unused CW bridge stays off. SG is an advisory stall guard here, not
an encoder or proof of motion. Require three consecutive invalid or near-zero
reads to abort; reset that count across segment idle gaps.

The initial velocity bracket at fixed 128/2 produced:

| Velocity | Conservative 95% upper bound | Disposition |
|---|---:|---|
| 2 mm/s | -65.36 dBFS | provisional -60 / -63 pass |
| 4.25 mm/s | -60.81 dBFS | provisional -60 pass |
| 6 mm/s | -55.59 dBFS | reject: too loud and planner underruns |

The 6 mm/s run completed naturally but recorded 395 empty-queue timer ticks
near the start of its first return leg. The cause is not established; increased
diagnostic polling is a hypothesis, not a finding. Do not use this run to
qualify motion. Its subsequent bounded endpoint audit passed with contacts
2472, 2502 and 2501 steps and three microphone rises above 8 dB. Raw evidence
is retained. The host now records the underrun count before each repeat and
requires it to remain unchanged; an increase or reset fails that repeat.
Historical fault counts do not silently disqualify unrelated later runs.

Changing `PWM_GRAD` from 2 to 0 gave provisional upper bounds of -62.13 dBFS
at 4.25 mm/s, -61.51 at 5 mm/s and -59.68 at 6 mm/s, all with clean planners.
The 6 mm/s result is 0.32 dB above the ceiling: retain it for optional listening,
not automatic acceptance. At 5 mm/s, offset 124 / gradient 0 measured -62.70,
but another underrun burst invalidated that run. Repeat it before comparison.

Full driver HTTP requests measured roughly 110-125 ms each. They hold the
planner mutex through a large UART register scan, so in-motion monitoring can
interfere with queue generation. A new optional `?motionHealth=true` dump
returns six checked registers (IOIN, GCONF, GSTAT, CHOPCONF, DRV_STATUS,
SG_RESULT). It measured about 10.3 ms of UART time per driver and 51-54 ms
median HTTP time at idle. It preserves fault, setup, hardware-enable,
software-bridge, interpolation and SG checks, including the unused CW bridge.
All mandatory health reads use CRC/framing validation and fail closed; SG
retains the three-consecutive-read rule. No homing settings or pulse generation
changed. Service and production images build, native assertions and 38 Python
tests pass. A bounded post-upload homing audit also passed.

Use `--lightweight-driver-polling` for subsequent RHO trials. The host verifies
support before any motion and still takes full register dumps before and after
segments. Recheck the same acoustic setting with the new polling before
resuming the sweep; the underrun cause is not considered proven merely by this
timing improvement.

The first lightweight-polling repeat (200 mA, fixed 124/0, u8, 5 mm/s) had
zero underruns, valid timing and an upper bound of -60.66 dBFS. It supports
using the lighter monitor, but does not establish offset 124 as quieter than
128. Retain 128 while bracketing speed; use repeated qualification to avoid
choosing a driver setting from one favourable background window.

With lightweight polling, fixed 128/0 at 5.5 mm/s had no underruns but missed
the -60 ceiling at -59.78 dBFS. Keep 5 mm/s as the fastest passing screen,
pending repeated qualification; do not round the 5.5 result into a pass.
The quieter fixed-128/0 screens passed -63 at 3 mm/s (-64.76 dBFS upper
bound) and 3.5 mm/s (-63.21). The latter has little margin and particularly
needs independent repeats; neither is yet a qualified assembled profile.
At 2 mm/s, fixed 128/0 subsequently screened near home at -66.66 dBFS upper bound
(-67.94 estimated motor excess), with valid background/timing checks and no
faults. This is a provisional -66 pass, unlike the earlier floor-limited result.

Retain the -60, -63, and -66 dBFS RHO tiers. Use four-leg 50 mm screens, then
two independent eight-leg qualifications of finalists. Require each gate to
sustain commanded speed; enlarge to 100 mm if necessary. Search velocity by
bracketing/bisection within a stable driver family, and compare discrete PWM,
interpolation, and chopper variants separately because resonances need not be
monotonic. Validate the chosen profile over 400 mm and reversal stress before
promotion. Record a noise-floor-limited tier as inconclusive, not inaudible.

The main-only service boot baseline is 200 mA run/hold, u8 interpolated,
fixed PWM 128/2, 1 mm/s, acceleration 2, and jerk 10. Normal motion tuning
retains its conservative CS14 cap; the user separately authorized the dedicated
homing profile near the 500 mA rating. The 425 mm travel is nominal: an outer
stop contact invalidated a submillimetre command-ledger reference during homing
qualification. Keep acoustic targets at or below 400 mm.

The acoustic trajectory itself does **not** home rho. Separate, explicitly
recorded endpoint audits use the qualified homing method in this session.
For initial commissioning, the operator places
the mechanism at a known, marked physical start, then explicitly switches the
service image from Manual RHO to RHO Commissioning. That confirmation assigns
the current physical position temporary logical `rho=0`.
Every test target is between logical 0 and +400 mm, and every successfully
completed test returns to logical 0. No test commands an inward-negative rho
position.

The host calibration helper now stops on every exception, including a failed
HTTP request or keyboard interrupt, instead of allowing an unobserved AT#2
segment to finish. It also checks both travel bounds, bridge health, cruise SG
and planner underruns. Automatic-gradient experiments still use full register
polls to validate PWM adaptation, so reject any resulting planner underrun;
do not infer calibration success from a completed command alone. The current
automatic-current / manual-gradient comparison does not require AT#2 motion.

## Test trajectories

- Screen: four complete 50 mm legs, `0 -> 50 -> 0 -> 50 -> 0`, with an idle
  gap after each leg. If every leg does not sustain at least 90% of commanded
  velocity for one second, repeat the screen at 100 mm.
- Offset screen: `--rho-window-start-mm 50 --rho-excursion-mm 50` runs
  `0 -> 50 -> 100 -> 50 -> 100 -> 50 -> 0` (300 mm total), exposing the noisy
  return section without a full-range trip for every candidate. Entry and
  exit legs are also measured, never used to average away a louder gate. The
  host rejects negative offsets and any window extending beyond 400 mm before
  contacting the board. Offset windows apply only to verify/screen/gated/ramp.
- Continuous: `start -> start+400 mm -> start`.
- Spatial range: `--profile range --rho-excursion-mm 400` travels outward
  in eight 50 mm sections and back over the same sections, pausing after each.
  Its 16 adjacent-idle comparisons expose position-dependent noise that a
  whole-trip median can hide. It totals 800 mm and returns to zero. Require
  full cruise in every section; two repeats are needed for qualification.
- Gated qualification: eight complete 50 or 100 mm legs with confirmed idle
  gaps after every leg. This is the primary numeric acoustic trajectory in a
  changing room environment.
- Stress: reversal-heavy moves through offsets from 0 to +400 mm, followed by
  an explicit return to start.
- Ramp: with `--rho-excursion-mm 50`, the host runs
  `0 -> 5 -> 0 -> 10 -> 0 -> 20 -> 0 -> 50 -> 0` mm, with idle gaps.
  This provisional acceleration/jerk comparison totals 170 mm, always on the
  outward side. Unlike cruise qualification, short ramp legs need not reach
  maximum speed. Their metric includes the whole STEP-timed movement window
  and uses adjacent-idle-subtracted P95 power with its conservative upper
  bound. It is a separate transient metric, not a substitute for the cruise
  tier or the final 400 mm / reversal-stress checks. Enlarge the excursion if
  a faster candidate leaves too few independent audio frames in the shortest
  gates; insufficient data must not become a quiet pass.
- Theta remains stationary and its driver is not enabled by rho-commissioning
  firmware.

The host tool rejects a result unless telemetry shows that the sequence ended
within 0.05 mm of its logical start and never crossed inward of that start.
These are commanded/logical checks, not an encoder measurement. The operator
must still watch for missed motion and verify the mechanism physically returned
to the marked starting position.

## Commissioning firmware safety boundary

Use `esp32dev_rho_commissioning` or `esp32dev_rho_commissioning_ota` for ordinary
commissioning, or `esp32dev_rho_startup_trial_ota` when auditing the frozen
production homing entry as in this session. These guarded service builds:

- boots in Manual RHO mode without claiming a physical origin, and permits
  relative RHO jogs for setup;
- switches to bounded commissioning only after explicit physical-origin
  confirmation, so no reflash is needed between manual and test control;
- allows independently capped known-position homing in RHO Commissioning mode;
  see `RHO_HOMING_TUNING.md` for current bounds and production startup status;
- disables manual moves, patterns, clearing, and theta tuning tests;
- explicitly disables the theta driver and verifies `TOFF=0` over UART, which
  also makes warm OTA transitions safe;
- enables only the configured rho motor drivers and verifies unused bridges off;
- boots with `VSENSE=1`, 200 mA requested run/hold current, 8 external microsteps,
  StealthChop, CoolStep off, 1 mm/s velocity, 2 mm/s² acceleration, and
  10 mm/s³ jerk;
- resets the full chopper, PWM, interpolation, hold-delay, and standstill
  profile to its checked baseline on every commissioning boot instead of
  inheriting the previous trial;
- uses the nominal 425 mm RHO travel limit;
- permits only bounded rho continuous/stress generators and commissioning-only
  absolute segment targets from logical 0 through +400 mm.

A trial may apply different settings after the operator supplies the motor's
rated RMS phase current. The current conversion uses the FYSETC V3.0
manufacturer value of 0.11 ohm for its external sense resistors. Firmware sets
`GCONF.I_SCALE_ANALOG=0`, so UART controls current and the onboard VREF
potentiometer does not set running current after configuration. Both rho
drivers must confirm digital current scaling and external sensing over UART.

The 500 mA motor rating remains a ceiling. For regulated-current operation,
until you measure coil current or
confirm the shunt tolerance, firmware caps rho at raw current code CS=14. With
`VSENSE=1`, the useful commissioning points are:

| CS | Nominal RMS current |
|---:|---:|
| 8 | 275 mA |
| 10 | 337 mA |
| 12 | 398 mA |
| 13 | 428 mA |
| 14 | 459 mA |

CS=14 is the high-current tuning point: about 486 mA after the firmware's 6%
uncertainty allowance. CS=15 commands about 490 mA nominal and about 519 mA
with the same allowance, so the API and host tool reject it.
These register-code checks do not bound coil current in unregulated fixed-PWM
mode; see the current-setting caveat above.

UART cannot protect the interval before the controller configures the driver
after a cold power-on. Hold the module's active-low `ENN` pin high during boot,
or set VREF to a conservative current as a bootstrap limit if the carrier ties
`ENN` low. Do not tune VREF for normal operation; UART owns the operating
current.

On this machine, `IOIN.ENN` read low on both RHO drivers during commissioning.
Set each module to about 0.39 V VREF before reconnecting a motor. FYSETC's
`VREF = I_RMS * 1.41` formula maps 275 mA RMS to 0.388 V. Keep the motor phases
disconnected, apply VM power, measure VREF to GND, adjust with an insulated
tool, then remove power before reconnecting the phases. This setting limits a
cold-boot interval; UART still sets CS8 after startup.

Do not use the general bench-motion build for this procedure. It does not
provide the rho-only motion lockout or the host-side commissioning identity
check.

## Hard safety rules

1. With driver power off, put the mechanism at the marked, repeatable test
   start near physical home. Confirm at least 400 mm of clear outward travel
   remains.
2. Proceed only if the installed machine's existing `+rho` STEP/DIR direction
   is already known to move physically outward. This test cannot safely
   discover a reversed direction without first violating the inward boundary.
3. Do not reposition, connect, or disconnect motor phases while driver power is
   applied.
4. Keep physical power cutoff and `POST /api/motion/stop` immediately
   available.
5. Stop for scraping, impact, a stationary motor, unequal paired motion,
   racking, skipped steps, excessive vibration, thermal warnings, or driver
   faults.
6. A stop, timeout, communication failure, or interrupted process invalidates
   the temporary origin. Do not ask firmware to return blindly. Power down,
   manually restore the marked physical start, then reboot commissioning
   firmware.
7. Never continue a test if either rho driver is missing or UART-invalid.
8. Start each new current/microstep family with a single continuous screen.
   Run stress only after outward and return directions have been observed.
9. Keep at least 20-25% headroom below the firmware's 10 kHz per-axis STEP
   budget. Rho step rate is `velocity_mm_s * 50 * external_microsteps`.

## Microphone setup

Use the existing Antlion source:

```text
alsa_input.usb-Antlion_Audio_Antlion_USB_Microphone-00.mono-fallback
```

Mechanically fix its location and orientation so it captures the complete rho
mechanism without touching the frame. Because one microphone hears both rho
motors together, it selects the quietest complete mechanism; it cannot identify
which individual motor produced a line.

Use mono signed 16-bit PCM at 48 kHz. Record the PulseAudio/PipeWire source,
capture volume, base volume, and mute state. The tool records this fingerprint
before and after every repeat and rejects comparisons after a change. Theta's
old dBFS ceilings do not transfer to rho, even with the same microphone.

## Preparation for a later powered session

Do not run these commands until the mechanism is ready and the operator has
authorized firmware upload and motion.

```bash
pgrep -af 'acoustic_tuner|pw-record|pio'
pactl list short sources | rg 'Antlion.*Microphone'
pactl get-source-mute \
  alsa_input.usb-Antlion_Audio_Antlion_USB_Microphone-00.mono-fallback
pactl get-source-volume \
  alsa_input.usb-Antlion_Audio_Antlion_USB_Microphone-00.mono-fallback
```

After placing the mechanism at its marked physical start, install the guarded
image and wait for the reboot:

```bash
pio run -e esp32dev_rho_commissioning_ota -t upload
```

Then require all of the following:

```bash
curl -fsS http://100.76.149.200/api/status
curl -fsS http://100.76.149.200/api/motion/telemetry | jq
curl -fsS http://100.76.149.200/api/tuning | jq
curl -fsS http://100.76.149.200/api/tuning/dump/rho | jq
curl -fsS http://100.76.149.200/api/tuning/dump/rho-companion | jq
curl -fsS http://100.76.149.200/api/errors | jq
```

- state is `IDLE`;
- `commissioningAxis` is exactly `rho`;
- logical rho position and both velocities are zero;
- both rho dumps have `uartResponseValid=true` and `setupOk=true`;
- the firmware reached `IDLE`, proving theta disable verification and both-rho
  setup succeeded;
- both drivers report interpolation to 256 and no thermal, short, undervoltage,
  or driver-error flags;
- theta is unavailable and stationary;
- no recorder or prior tuning process is running.

The Python tool repeats these motion-critical checks and refuses a rho trial on
production or theta-commissioning firmware.

## Rho acoustic acceptance tiers

Discard the old `-55 dBFS` provisional limit and the later `-58.1 dBFS`
working limit for acceptance. The operator selected `-60 dBFS` as the fixed
maximum acceptable sustained-cruise motor-excess ceiling for the current
hardware session. Retain three distinct, mechanically reliable profiles:

- acceptable: every qualifying gate is at or below `-60 dBFS`;
- quiet -3 dB: every qualifying gate is at or below `-63 dBFS`;
- quiet -6 dB: every qualifying gate is at or below `-66 dBFS`.

Apply these limits to the adjacent-idle-subtracted, A-weighted broadband
motor-excess metric and the raw-total/whole-STEP checks in the current retune
section above. The raw checks prevent subtraction of energized hold noise
from creating a false pass. Timing-locked tone levels use a different
calculation and remain corroborating diagnostics until the operator selects a
separate tone ceiling; do not compare a tone dBFS value directly with these
broadband limits. The microphone position, gain, load state, and timing-quality
checks must remain fixed across all three tiers.

First record stationary room/mechanism noise:

```bash
python scripts/acoustic_tuner.py baseline \
  --duration 10 --settle 3
```

Use a 50 mm gated screen for coarse candidates. It remains provisional after a
pass.

```bash
python scripts/acoustic_tuner.py trial \
  --axis rho --reference-only \
  --label rho-screen-4u-cs8-v4-a20-j100 \
  --rated-current-ma 500 \
  --profile screen --rho-excursion-mm 50 --repeats 1 \
  --pre-idle 5 --gated-idle 2 --duration 120 --post-idle 5 \
  --run-current-ma 275 --hold-current-ma 100 \
  --current-scale high \
  --velocity 4 --accel 20 --jerk 100 \
  --microsteps 4 --mode stealthchop --coolstep off
```

If this setting cannot move both motors cleanly, stop and inspect mechanics,
wiring, and current telemetry. Do not interpret a stalled low-current run as an
acoustic candidate. Increase current only through the staged CS8, CS10, CS12,
CS13, and CS14 points.

Once motion is visibly healthy, record at least two gated repeats using the
current known-reliable profile. The repeated motor-on / motor-off intervals
allow broadband motor energy to be distinguished from a changing background:

```bash
python scripts/acoustic_tuner.py trial \
  --axis rho \
  --label rho-acceptable-60dbfs \
  --rated-current-ma RATED_CURRENT \
  --acceptable-ceiling-dbfs -60 \
  --tone-ceiling-dbfs TONE_CEILING \
  --profile gated --rho-excursion-mm 50 --repeats 2 \
  --pre-idle 5 --gated-idle 2 --duration DURATION --post-idle 5 \
  --run-current-ma CURRENT --hold-current-ma HOLD \
  --velocity VELOCITY --accel ACCEL --jerk JERK \
  --microsteps MICROSTEPS --mode stealthchop --coolstep off
```

The qualifying broadband value is the loudest gate in any repeat, measured only
while telemetry confirms at least 90% of commanded velocity. The analyzer
subtracts the louder adjacent-idle A-weighted power in linear units before
converting that gate's motor excess to dBFS. Quiet acceleration ramps therefore
cannot dilute the result. A gated broadband result qualifies only when every
expected local on/off pair is available, at least 75% show the sound rising with
motion and falling at idle, every gate sustains cruise for at least one second,
background halves are stable, microphone gain is unchanged, audio is not
clipped, and telemetry timing passes. The separately named raw high-speed value
is room-plus-motor diagnostic data and is never the acceptance metric. All
readings are relative digital dBFS, not SPL.

Thresholds derived from earlier whole-leg averages are not comparable with the
fixed sustained-cruise limits above.

The recorder captures raw PCM and timestamps the first delivered audio block;
it does not assume process launch equals sample zero. Telemetry GETs are timed
at the midpoint of their request and run independently from slower driver
diagnostics. The tool stores request RTT, p95/max sample gaps, every start/stop
bracket, and audio epoch uncertainty. Any failed timing check invalidates the
acoustic comparison even if the motion itself completed safely.

`--duration` is a per-sequence safety timeout, not the recording duration. The
continuous sequence travels 800 mm total; the stress sequence travels 5,950 mm
total. The tool rejects a timeout shorter than 125% of `distance / velocity`
plus 10 seconds, but short reversal ramps can require more time. Increase the
timeout rather than increasing velocity merely to fit a recording window.

## Candidate trial

After choosing rho-specific ceilings, remove `--reference-only`:

```bash
python scripts/acoustic_tuner.py trial \
  --axis rho \
  --label rho-candidate \
  --rated-current-ma RATED_CURRENT \
  --acceptable-ceiling-dbfs BROADBAND_CEILING \
  --tone-ceiling-dbfs TONE_CEILING \
  --minimum-persistence 0.65 \
  --profile gated --repeats 2 \
  --pre-idle 5 --gated-idle 2 --duration DURATION --post-idle 5 \
  --run-current-ma CURRENT --hold-current-ma HOLD \
  --velocity VELOCITY --accel ACCEL --jerk JERK \
  --microsteps MICROSTEPS --mode stealthchop --coolstep off
```

The tool sets the UI speed multiplier to 10/10, persists requested values,
records one timestamped Antlion stream per repeat, polls motion telemetry on a
path independent from both driver dumps, and writes WAV, timeline JSON, timing
plot, detailed result JSON, and append-only `results.jsonl` artifacts under
`tuning-recordings/`.

## Search order

Change one family at a time:

1. **Reliable current:** find the lowest current that completes both directions
   and stress without missed motion. Check nearby values because current moves
   resonances rather than simply changing volume.
2. **External microsteps:** compare 2, 4, 8, and 16 first. Higher values sharply
   reduce the available velocity under the 10 kHz step budget. Require
   both drivers to read back the requested interpolation state.
3. **Velocity:** bracket upward and downward around repeatable tonal peaks. Do
   not assume slower is quieter. Use continuous runs to judge sustained sound.
4. **Acceleration and jerk:** tune with the stress profile at fixed current,
   microsteps, and velocity. A gentler ramp can dwell in a resonance; test both
   sides of any apparent improvement.
5. Keep StealthChop and automatic current scaling as the first quiet mode.
   Automatic gradient adaptation is a separate choice: retain it only when
   both paired drivers converge under representative loads. Keep CoolStep off
   until fixed-current behavior is repeatable. Test SpreadCycle or hybrid
   thresholds only if torque/stability requires it.

After the motion envelope passes, screen the driver controls in this order:

1. Compare all four `PWM_FREQ` values with automatic current scaling enabled.
   Try automatic gradient adaptation only after confirming that both motors
   converge rather than fighting their different loads.
2. Bracket `PWM_REG` and `PWM_LIM`, then test adjacent values around the best
   pair.
3. Record `PWM_OFS_AUTO`, `PWM_GRAD_AUTO`, `PWM_SCALE_SUM`, and
   `PWM_SCALE_AUTO` during sustained cruise. Use those automatic values as the
   center of a manual `PWM_OFS`/`PWM_GRAD` sweep.
4. Test `TOFF`, `TBL`, `HSTRT`, and `HEND` only for SpreadCycle or hybrid
   candidates. Reject `TOFF=1` with `TBL<2`, and keep raw
   `HSTRT + HEND <= 18` (effective hysteresis sum at most 16 for rho's capped
   current range). Keep protection-disable bits unchanged.
5. Tune `IHOLD`, `IHOLDDELAY`, `TPOWERDOWN`, and `FREEWHEEL` against idle sound,
   stop transients, and required holding torque. Keep `TPOWERDOWN >= 12` so
   the 200 ms AT#1 standstill interval completes before hold-current reduction.
6. Test CoolStep last. Sweep `SEMIN`, `SEMAX`, `SEUP`, `SEDN`, and
   `TCOOLTHRS` as a family after a fixed-current baseline passes.

The tool exposes these fields through `--pwm-*`, `--automatic-current`,
`--automatic-gradient`, `--chopper-off-time`, `--blank-time`,
`--hysteresis-*`, `--hold-delay`, `--power-down-delay`, `--standstill-mode`,
and `--coolstep-*`. Change one field or coupled pair per screen. Run the 400 mm
continuous and stress profiles for finalists.

Use one repeat for screening, two independent repeats for confirmation, and
five continuous plus five stress repeats for the selected soak profile.
Use gated repeats—not continuous runs—for numeric broadband acceptance whenever
the room background changes. Continuous runs remain useful for operator A/B
listening and position-dependent spectrograms.

## 2026-09-08 rejected asymmetric-load candidate

This result is test evidence, not a production default. The RHO-CW
motor carried its load while the main RHO motor was unloaded. The operator had
also observed low-current slipping near home, so this session deliberately
held both run and standstill current at the highest allowed CS14 point. Do not
reduce current or enable CoolStep until that mechanical issue is resolved.

The operator-audible reference was reprocessed with the sustained-cruise
metric described above. Its loudest cruise gate was -34.56 dBFS and its
cruise-locked tone was -52.00 dBFS. The provisional limits are 3 dB quieter:
-37.56 dBFS broadband motor excess and -55.00 dBFS for a locked tone. These
limits replace all earlier whole-leg and pre-load rho thresholds.

The candidate asymmetric-load profile was:

| Setting | Selected value |
|---|---:|
| Velocity | 14.5 mm/s |
| Acceleration | 5 mm/s² |
| Jerk | 100 mm/s³ |
| Run / hold current | CS14 / CS14, about 459 mA RMS nominal |
| Current sense range | high sensitivity, external 0.11 ohm shunts |
| External microsteps | 2, with interpolation to 256 enabled |
| Chopper mode | StealthChop at all tested speeds |
| `PWM_FREQ` | 0 |
| `PWM_REG` / `PWM_LIM` | 15 / 15 |
| Automatic current scaling | enabled |
| Automatic gradient adaptation | disabled |
| Manual `PWM_OFS` / `PWM_GRAD` | 119 / 10 |
| CoolStep | disabled |
| Hold delay / power-down delay | 8 / 20 |
| Standstill mode | normal |

CS14 is intentionally close to the 500 mA RMS motor rating: about 459 mA
nominal and about 486 mA at the firmware's conservative 6% uncertainty edge.
CS15 remains prohibited because that same calculation reaches about 519 mA.

Two independent, timing-valid 100 mm gated samples passed both provisional
microphone limits at CS14 run and hold current:

| Recording | Broadband motor excess | Loudest locked tone |
|---|---:|---:|
| `20260908T092621Z-...-qualified`, valid repeat 2 | -39.49 dBFS | -56.73 dBFS |
| `20260908T093628Z-...-qualification-final` | -38.81 dBFS | -56.54 dBFS |

The candidate initially completed a 400 mm continuous round trip and the full
5,950 mm reversal stress trajectory. Both ended at logical zero, never
commanded inward of zero, kept theta stationary, reported no planner underruns,
and left both RHO drivers UART-valid and fault-free. Continuous and stress
recordings are safety and listening evidence only; without adjacent idle gates
they are not numeric acoustic qualifications.

A second operator-listening 400 mm pass rejected the candidate. The operator
reported that it was plainly loud and that the mechanism appeared to stick on
the return. The run was stopped at logical rho 201.17 mm and firmware entered
`INITIALIZED` with `requiresHoming=true`. Both RHO drivers still reported valid
UART, CS14, no electrical or thermal fault, and zero planner underruns. Those
facts do not prove physical motion: STEP/DIR telemetry counts commanded steps
and cannot detect a stalled or slipping rotor. The temporary logical position
was therefore invalidated and no commanded recovery was attempted.

The sweep selected 2 external microsteps with interpolation, `PWM_FREQ=0`,
`PWM_REG=15`, `PWM_LIM=15`, automatic current scaling, and manual gradient
values. Automatic gradient adaptation could not converge both differently
loaded motors. SpreadCycle was about 5.2 dB louder in the controlled baseline
and was rejected. Speeds at and above 15 mm/s were marginal or failed the
corrected numeric limits. Although 14.5 mm/s passed the short gated metric, the
operator and failed return override that result. No acoustically and
mechanically accepted asymmetric-load profile was selected.

Before another powered test, power down and manually restore a known physical
start; do not trust or command from the interrupted run's logical position.
Inspect the return-path alignment and source of sticking first. Resume with a
short, observed, lower-speed outward/return screen before any 400 mm pass.
After attaching the main RHO load, restore the same microphone position and
gain, establish a new operator-audible reference, and rerun the parameter
screens plus gated qualification, 400 mm continuous, stress, and soak tests.
Current reduction, CoolStep, hold-current reduction, automatic gradient
adaptation, and final production defaults all remain deferred. The eventual
full-load acoustic profile must be frozen before RHO homing thresholds are
tuned.

## 2026-09-09 final-load results

Both RHO mechanisms carried their intended final loads, alignment had been
corrected, total usable travel was measured as 425 mm, and the operator
manually established physical zero. The Antlion remained at 100% / 0.00 dB.
The stationary A-weighted room baseline was -57.87 dBFS.

The selected common driver profile is:

| Setting | Value |
|---|---:|
| Run / hold current | 150 / 75 mA requested |
| External microsteps | 8, interpolated to 256 |
| Chopper | StealthChop always; CoolStep off |
| `PWM_FREQ` / `PWM_REG` / `PWM_LIM` | 0 / 15 / 8 |
| Automatic current / gradient | off / off |
| `PWM_OFS` / `PWM_GRAD` | 128 / 2 |
| `TOFF` / `HSTRT` / `HEND` / `TBL` | 3 / 5 / 0 / 2 |
| Acceleration / jerk | 20 mm/s² / 100 mm/s³ |

The high-impact TMC2209 controls were screened. `PWM_FREQ=0`, 8 external
microsteps, interpolation on, fixed 128/2 PWM feed-forward, and 150 mA were
best. Interpolation off, 4 microsteps, fixed 124/3, and 100 mA were materially
louder. At 4 mm/s the 100 mA conservative bound was -56.37 dBFS, versus
-62.01 dBFS at 150 mA; reduced current did not reduce mechanism sound.
CoolStep and SpreadCycle were already rejected as louder in controlled
screening. Hold timing, freewheel, and SpreadCycle hysteresis controls do not
improve the sustained StealthChop cruise metric and retain their safe values.

Two independent eight-leg qualifications selected:

| Tier | Velocity | Worst measured motor excess | 95% upper bound | Result |
|---|---:|---:|---:|---|
| acceptable | 4.25 mm/s | -64.40 dBFS | -60.91 dBFS | qualified at -60 |
| quiet -3 dB | 2.00 mm/s | -69.66 dBFS | -63.07 dBFS | qualified at -63 |
| quiet -6 dB | 1.00 mm/s | -71.78 dBFS (screen) | -63.99 dBFS | best effort; confidence floor prevents -66 qualification |

The operator had already judged 1 mm/s completely inaudible. No repeatable
motion-locked tone was found in its screen. Nevertheless, the changing-room
noise leaves a roughly -64 dBFS statistical upper-bound floor, so this record
does not claim a conservative -66 qualification. A quieter room, closer fixed
microphone geometry, or an additional independent sensor is required to make
that numerical claim.

The 4.25 mm/s profile completed a full `0 -> 400 -> 0` range test with both
drivers healthy, no planner underruns, no sustained in-motion SG collapse, and
an exact logical return. The continuous recording is range evidence, not a
numeric acoustic qualification because it lacks adjacent idle around each
directional gate.

## Acceptance rules

A candidate passes only when:

- the operator saw both rho mechanisms move together without stalls or racking;
- the operator did not hear objectionable motion;
- every sequence completed naturally and returned physically to the marked
  start;
- telemetry reports `allSequencesReturnedToStart=true` and
  `rhoNeverMovedInwardOfStart=true`;
- theta stayed stationary;
- both rho UART streams and interpolation readbacks stayed valid;
- no driver fault or thermal warning appeared;
- no new planner underruns occurred during the repeat, and maximum consecutive
  underruns stayed zero;
- repeated timing-locked tones stayed beneath the selected rho ceiling;
- the loudest adjacent-idle-subtracted sustained-cruise gate in every repeated
  gated trial stayed beneath its selected ceiling, with every expected on/off
  pair present and at least 75% on/off consistency;
- telemetry cadence, transition brackets, and audio sample-zero uncertainty
  passed their timing limits;
- changing-background, clipping, and gain checks all passed;
- microphone gain and placement stayed unchanged;
- the board returned to `IDLE` after every repeat.

`CS_ACTUAL` is programmed current scaling, not measured coil current. Logical
position proves emitted planner steps, not rotor position. Operator observation
remains mandatory.

## Recovery

An interrupted/failed run sends the stop endpoint and terminates the recorder.
Do not start another trial merely because the process exited. Verify the
process is gone, power down, put the mechanism back on its physical start mark,
and reboot so temporary logical zero is re-established.

```bash
pkill -INT -f '^python scripts/acoustic_tuner.py trial'
curl -fsS -X POST http://100.76.149.200/api/motion/stop
```

Do not use a firmware-commanded inward recovery after an abnormal stop.

## Handoff to homing

The selected rho current, microsteps, StealthChop/PWM mode, supply voltage, and
mechanical configuration become fixed inputs to StallGuard homing calibration.
Tune homing only after this acoustic profile is selected. Any later rho change
requires homing requalification. After homing is qualified, restore production
firmware and repeat a smaller coupled-axis acoustic and motion regression.

Raw recordings and plots remain ignored under `tuning-recordings/`. Commit only
the selected settings, compact results, caveats, and decisions.
