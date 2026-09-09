# Rho commissioning and acoustic tuning playbook

Use this playbook to select quiet, reliable settings for the paired rho motors
before tuning sensorless homing. It uses the same Antlion USB microphone and
the telemetry-aligned analysis in `scripts/acoustic_tuner.py`.

The acoustic procedure deliberately does **not** home rho. The operator places
the mechanism at a known, marked physical start, then explicitly switches the
service image from Manual RHO to RHO Commissioning. That confirmation assigns
the current physical position temporary logical `rho=0`.
Every test target is between logical 0 and +400 mm, and every successfully
completed test returns to logical 0. No test commands an inward-negative rho
position.

## Test trajectories

- Screen: four complete 50 mm legs, `0 -> 50 -> 0 -> 50 -> 0`, with an idle
  gap after each leg. If every leg does not sustain at least 90% of commanded
  velocity for one second, repeat the screen at 100 mm.
- Continuous: `start -> start+400 mm -> start`.
- Gated qualification: eight complete 50 or 100 mm legs with confirmed idle
  gaps after every leg. This is the primary numeric acoustic trajectory in a
  changing room environment.
- Stress: reversal-heavy moves through offsets from 0 to +400 mm, followed by
  an explicit return to start.
- Theta remains stationary and its driver is not enabled by rho-commissioning
  firmware.

The host tool rejects a result unless telemetry shows that the sequence ended
within 0.05 mm of its logical start and never crossed inward of that start.
These are commanded/logical checks, not an encoder measurement. The operator
must still watch for missed motion and verify the mechanism physically returned
to the marked starting position.

## Commissioning firmware safety boundary

Use only `esp32dev_rho_commissioning` or
`esp32dev_rho_commissioning_ota`. This single service build:

- boots in Manual RHO mode without claiming a physical origin, and permits
  relative RHO jogs for setup;
- switches to bounded commissioning only after explicit physical-origin
  confirmation, so no reflash is needed between manual and test control;
- allows only confirmed-origin, runway-plus-1-mm bounded homing while in RHO
  Commissioning mode; unknown-position production homing remains unavailable;
- disables manual moves, patterns, clearing, and theta tuning tests;
- explicitly disables the theta driver and verifies `TOFF=0` over UART, which
  also makes warm OTA transitions safe;
- enables only the paired rho drivers;
- boots with `VSENSE=1`, `IRUN=8`, about 275 mA RMS run current, 100 mA hold,
  2 external microsteps,
  StealthChop, CoolStep off, 1 mm/s velocity, 2 mm/s² acceleration, and
  10 mm/s³ jerk;
- resets the full chopper, PWM, interpolation, hold-delay, and standstill
  profile to its checked baseline on every commissioning boot instead of
  inheriting the previous trial;
- uses the measured 425 mm usable RHO travel limit;
- permits only bounded rho continuous/stress generators and commissioning-only
  absolute segment targets from logical 0 through +400 mm.

A trial may apply different settings after the operator supplies the motor's
rated RMS phase current. The current conversion uses the FYSETC V3.0
manufacturer value of 0.11 ohm for its external sense resistors. Firmware sets
`GCONF.I_SCALE_ANALOG=0`, so UART controls current and the onboard VREF
potentiometer does not set running current after configuration. Both rho
drivers must confirm digital current scaling and external sensing over UART.

The 500 mA motor rating remains a ceiling. Until you measure coil current or
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

These limits apply only to the adjacent-idle-subtracted, A-weighted broadband
motor-excess metric described below. Timing-locked tone levels use a different
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
- planner underruns and maximum consecutive underruns stayed zero;
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
