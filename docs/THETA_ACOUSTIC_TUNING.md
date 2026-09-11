# Theta commissioning and acoustic tuning playbook

Use this playbook to re-establish the fastest useful theta settings at the
operator-selected noise limits. It is written for an agent working with the
operator present. The operator's report always overrides a microphone pass.

Theta is a continuous rotational axis and has no physical homing operation.
`resetTheta()` assigns the current angle as logical zero. Sensorless homing and
the UI confirmation flow apply only to rho.

## Current validated hardware context

- ESP32 URL: `http://100.76.149.200`
- OTA environment: `esp32dev_theta_commissioning_ota`
- OTA password: `sandpatterns`
- Theta mechanism and motor are installed and mechanically loaded.
- Rho drivers and motors are disconnected during theta commissioning.
- No SD card is required for tuning tests.
- Close microphone: Antlion USB, fixed near the theta mechanism.
- The Logitech channel was removed from the process after it missed sounds the
  operator could hear. Historical artifacts may contain Logitech fields; ignore
  them for every comparison and acceptance decision.

The last operator-selected production candidate was 64 external microsteps, TMC2209
interpolation to 256, StealthChop, CoolStep off, 700 mA run / 200 mA hold,
0.48 rad/s velocity, 2 rad/s² acceleration, and 10 rad/s³ jerk. Re-run the
procedure after mechanical, microphone, motor, driver, supply, or mounting
changes rather than assuming those values still apply. The 2026-09-11 loaded
retune below found a quieter 16-microstep driver profile, but it does not become
the production selection until the operator listens to and chooses a tier.

## Sources of truth

Do not confuse the selected production profile with the theta-commissioning
boot envelope:

| Scope | Velocity / accel / jerk | Current | Purpose |
|---|---|---|---|
| Production fallback | 0.48 rad/s / 2 rad/s² / 10 rad/s³ | 700 mA run / 200 mA hold | Used when no valid saved tuning exists |
| LittleFS tuning store | Query with `GET /api/tuning` | Query with `GET /api/tuning` | Runtime/production authority when persistence is available |
| Theta commissioning after reboot | 0.05 rad/s / 0.10 rad/s² / 0.50 rad/s³ | 250 mA run / 100 mA hold | Deliberately safe start; a trial applies its requested values |

The production fallback and commissioning boot envelope use 64 external
microsteps, StealthChop, and CoolStep off. The
production fallback lives in `MotionSettings` plus the `PolarControl`
constructor. The commissioning override lives in `PolarControl::begin()` and
uses the current constants in `Config.h`. A tuning trial posts and persists its
requested values, but a commissioning reboot still replaces the runtime values
with its safe envelope. A production boot will load the persisted profile.

The historical operator-selected production driver record is:

| Setting | Value |
|---|---:|
| Run current | 700 mA RMS |
| Hold current | 200 mA RMS |
| Hold delay | 8 register steps |
| External microsteps | 64 |
| TMC interpolation | 256, enabled and read back |
| StealthChop | Enabled |
| StealthChop threshold | 0 (always) |
| CoolStep | Disabled |
| CoolStep lower / upper threshold | 1 / 0 |
| CoolStep current increment / measurement count / threshold | 0 / 0 / 0 |

The Antlion calibration fingerprint was mono, signed 16-bit PCM at 48 kHz,
capture volume 100% / 0.00 dB, base volume 56% / -15.00 dB, and unmuted. Any
change to those values starts a new calibration session.

## Final retune targets (2026-09-11)

The current campaign replaces the old three relative line tiers with an
integer ladder of explicit sustained-motor-excess targets. The earlier
`-55 dBFS` performance tier was dropped after live listening:

| Tier | Maximum adjacent-idle-subtracted A-weighted motor excess |
|---|---:|
| -58 | -58 dBFS |
| -59 | -59 dBFS |
| -60 | -60 dBFS |
| -61 | -61 dBFS |
| -62 | -62 dBFS |
| -63 | -63 dBFS |
| -64 | -64 dBFS |
| -65 | -65 dBFS |

These thresholds apply to the loudest telemetry-confirmed constant-velocity
gate and its conservative 95% upper bound. They do not apply numerically to an
FFT bin: motion-locked spectral lines are recorded separately with their own
absolute dBFS values and must disappear in adjacent idle gates. The operator's
listening verdict is still required before calling any tier inaudible.

For each tier, first minimize driver noise at the known 0.48 rad/s reference,
then find the highest velocity that passes, then maximize acceleration and
jerk without creating audible reversal or ramp noise. A faster velocity is not
accepted merely because a slower ramp lowers the whole-window average; every
scored gate must sustain at least 90% of commanded velocity for one second.

### Loaded retune result

With the loaded theta mechanism and fixed Antlion setup, all eight requested
ceilings collapse into two measured velocity profiles. These are the fastest
points that completed the full eight-gate, two-repeat qualification in the
sampled search; they are not claims of a continuous mathematical maximum.

| Tier | Fastest qualified velocity | Conservative broadband upper bound | Confirmed limiting tone | First faster full-run failure |
|---|---:|---:|---:|---:|
| -58 | 0.548125 rad/s | -62.90 dBFS | none | 0.554062 rad/s: 70.31 Hz at -56.32 dBFS |
| -59 | 0.548125 rad/s | -62.90 dBFS | none | 0.554062 rad/s: 70.31 Hz at -56.32 dBFS |
| -60 | 0.548125 rad/s | -62.90 dBFS | none | 0.554062 rad/s: 70.31 Hz at -56.32 dBFS |
| -61 | 0.548125 rad/s | -62.90 dBFS | none | 0.554062 rad/s: 70.31 Hz at -56.32 dBFS |
| -62 | 0.548125 rad/s | -62.90 dBFS | none | 0.554062 rad/s: 70.31 Hz at -56.32 dBFS |
| -63 | 0.231250 rad/s | -65.18 dBFS | none | 0.234375 rad/s: -61.63 dBFS upper bound |
| -64 | 0.231250 rad/s | -65.18 dBFS | none | 0.234375 rad/s: -61.63 dBFS upper bound |
| -65 | 0.231250 rad/s | -65.18 dBFS | none | 0.234375 rad/s: -61.63 dBFS upper bound |

Both profiles use 700 mA run / 200 mA hold, 16 external microsteps with
interpolation to 256, always-on StealthChop, CoolStep off, standard current
scale, `TOFF=5`, `HSTRT=5`, `HEND=0`, `TBL=2`, `PWM_FREQ=1`, manual
`PWM_OFS=76` / `PWM_GRAD=23`, `PWM_REG=1`, `PWM_LIM=12`, hold delay 8,
power-down delay 20, and standstill mode 0. Motion uses 2 rad/s² acceleration
and 10 rad/s³ jerk. The search exercised microstep/interpolation choices,
standard and high-sensitivity current scaling, current, StealthChop PWM,
chopper timing, SpreadCycle, hybrid switching, CoolStep, velocity, acceleration,
and jerk. SpreadCycle, hybrid switching, CoolStep, 800 mA at the high-speed
edge, and alternate PWM families did not improve the loaded acoustic result.

The passing high-speed artifact is
`20260911T162221Z-theta-cert-tier58-v0p548125-current700-a2-j10-g23-result.json`.
The adjacent failing artifact is
`20260911T162646Z-theta-cert-fail-tier58-v0p5540625-current700-a2-j10-g23-result.json`.
The strict-profile artifact is
`20260911T160148Z-theta-cert-tiers63to65-v0p23125-current700-a2-j10-g23-result.json`,
and its adjacent failing artifact is
`20260911T152817Z-theta-cert-tier65-v0p234375-current700-a2-j10-g23-result.json`.
All four have healthy UART/readback, no driver or planner faults, stationary
rho, and exact return to the recorded theta start.

The -65 result has only 0.18 dB of measured margin. Use 0.225 rad/s as the
more conservative strict profile when repeatability matters more than the
2.8% speed increase; its full-run upper bound was -65.59 dBFS. Conversely,
0.548125 rad/s has 0.90 dB of margin at -62. Every listed pass still requires
the operator's listening approval before it may be described as inaudible.
The 0.548125 run also contained a 70.31 Hz, -58.11 dBFS line in only its second
repeat. It disappeared from both adjacent idle gates but was absent from the
first independent repeat, so the harness records it as a diagnostic rather
than a confirmed rejection. Listen especially carefully to that candidate
before accepting the -59 through -62 tiers; an intermittent human-audible line
overrides the repeat-based numeric pass.

These tier measurements cover sustained constant-velocity gates. They do not
claim that ramp/reversal transients meet the same numerical ceilings: the ramp
matrix found substantially louder position-dependent mechanism transients even
with gentler profiles. Among the tested practical motion profiles,
2 rad/s² / 10 rad/s³ retained the best balance, but final human listening and
the coupled rho/theta test remain mandatory.

## Current three-profile measurements (2026-09-07)

The fixed Antlion was knocked during one run, so that entire run was discarded
and the baseline was re-recorded after the microphone was fixed again. The
post-knock 82.03 Hz motion-locked mechanism line is the comparison metric.
The baseline's loudest repeat was `-58.45 dBFS`, making the quieter ceilings
`-61.45 dBFS` and `-64.45 dBFS`.

| Profile | Theta velocity | Line ceiling | Loudest rotation line | Loudest stress line | Step budget | Result artifact |
|---|---:|---:|---:|---:|---:|---|
| Baseline | 0.48 rad/s | -58.45 dBFS | -58.45 dBFS | -62.85 dBFS | 36.7% | `20260907T042330Z-theta-profile-baseline-confirm-64u-700ma-v0.48-a2-j10-result.json` |
| Baseline -3 dB | 0.225 rad/s | -61.45 dBFS | -61.77 dBFS | -62.93 dBFS | 17.2% | `20260907T044608Z-theta-profile-minus3-final-64u-700ma-v0.225-a2-j10-result.json` |
| Baseline -6 dB | 0.15 rad/s | -64.45 dBFS | -64.54 dBFS | -66.16 dBFS | 11.5% | `20260907T045023Z-theta-profile-minus6-final-64u-700ma-v0.15-a2-j10-result.json` |

All three use 64 external microsteps with interpolation to 256, StealthChop,
CoolStep off, 700 mA run / 200 mA hold, 2 rad/s² acceleration, and 10 rad/s³
jerk. Each measurement contains two continuous and two stress recordings with
zero planner underruns, stationary rho, valid driver UART, and interpolation
readback. The operator did not report audible noise during these passes.

The old harness stopped motion after about 40 seconds. Telemetry proves that
the baseline continuous captures completed the full forward-and-back rotation;
the 0.225 rad/s captures completed the forward rotation and part of the return,
and the 0.15 rad/s captures covered about 5.89 rad of the forward rotation.
The stress captures exercised 20-26 of the 64 generated moves before stopping.
The line levels remain useful fixed-session acoustic measurements, but the -3
dB and -6 dB alternatives are not full-cycle-qualified profiles. The harness
now requires every continuous/stress sequence to return to `IDLE` naturally;
a timeout safely stops motion and fails the run. Requalify all three tiers with
the revised harness after the next mechanical change or before selecting a
quieter tier for production.

These are current-mechanical-state references, not permanent constants. Every
future tuning agent must repeat the measurement procedure after lubrication,
belt-tension adjustment, bearing or guide work, mount changes, motor/driver or
supply changes, or microphone movement. Lubrication is expected to reduce
mechanism noise; when it does, search upward in velocity again to reclaim the
best performance beneath each newly calibrated ceiling rather than retaining
the old slower values automatically.

The original operator-approved broadband ceiling remains `-53.92 dBFS`
A-weighted. It is a secondary safety check only when both idle windows are
stable. Unrelated room noise repeatedly contaminated absolute broadband levels,
so it must not be used to manufacture a motor-noise failure. The motion-locked
line, repeated runs, timing plots, hardware telemetry, and operator report are
the primary gates. All dBFS values are relative digital levels, not calibrated
dB SPL. If the Antlion moves or its gain changes, discard the profile numbers
and recalibrate all three in the same fixed-mic session.

The operator selected **Baseline** as the production default on 2026-09-07
after a live rotation-and-stress listening pass. Firmware fallback defaults and
the board tuning store use 0.48 rad/s, 2 rad/s², 10 rad/s³, 64 microsteps,
700 mA run, and 200 mA hold. Commissioning firmware still forces its safe
250 mA / 0.05 rad/s envelope after every reboot. Keep the -3 dB and -6 dB
profiles available as measured alternatives; after mechanical changes, retune
rather than assuming any old profile remains optimal.

The final human-listening artifact is
`20260907T050228Z-theta-profile-baseline-human-listen-64u-700ma-v0.48-a2-j10-result.json`.
Its room/background levels were variable, so it is evidence of the operator's
selection and healthy board telemetry, not a replacement acoustic calibration.
The fixed-microphone `042330Z` artifact remains the baseline for relative line
levels.

## What this process proves

The test combines ESP32 telemetry with one continuous Antlion recording. It
can prove that commands were accepted, logical theta motion occurred on time,
STEP generation stayed within budget, driver UART/readback remained healthy,
rho stayed still, and a repeatable audible-band line appeared and disappeared
with commanded motion. It also exercises full rotations plus reversal-heavy
stress motion.

It cannot prove rotor angle, missed mechanical steps, available torque, or
actual coil current. There is no theta encoder. `CS_ACTUAL` reports the driver's
programmed current scale, not measured current. Human observation remains
required for smoothness/stalls, and the operator's hearing remains the final
inaudibility decision. All sound levels are relative dBFS, not calibrated dB
SPL.

The `continuous` test resets only the logical theta origin, then commands
0 -> 2π -> 0 at fixed rho: one complete revolution in each direction. The
`stress` test commands 64 out-and-back moves at fixed rho. Its ordered section
steps through 0.5°, 1°, 2°, 5°, 10°, 15°, 20°, 30°, 45°, 60°, and 90° and
back down; its second section repeats mixed 0.5°-90° amplitudes to emphasize
reversals and ramp transitions. Neither test physically homes theta.

## Safety rules

1. Never connect or disconnect motor phases with driver power applied.
2. Keep `POST /api/motion/stop` ready throughout every run.
3. Confirm the theta motor's rated RMS phase current before motion. Pass that
   value to every trial. Firmware and the tool impose a 1500 mA ceiling, but
   this is not a tuning target.
4. Stop immediately for scraping, impact, stalling, skipped motion, excessive
   vibration, driver faults, thermal warnings, or any operator noise report.
5. Do not run rho tests, homing, patterns, or manual motion in theta-only
   commissioning mode.
6. Keep at least 20-25% timing headroom and reserve practical CPU capacity for
   rho, networking, OTA, logging, and the Web UI.

## Start-of-session checklist

Follow the repository `AGENTS.md` memory and documentation-sync instructions.
Do not overwrite or discard unrelated dirty worktree changes.

```bash
pgrep -af 'acoustic_tuner|pw-record|pio'
pactl list short sources | rg 'Antlion.*Microphone'
pactl get-source-mute \
  alsa_input.usb-Antlion_Audio_Antlion_USB_Microphone-00.mono-fallback
curl -fsS http://100.76.149.200/api/status
curl -fsS http://100.76.149.200/api/motion/telemetry
curl -fsS http://100.76.149.200/api/tuning
curl -fsS http://100.76.149.200/api/tuning/dump/theta
curl -fsS http://100.76.149.200/api/errors
pactl get-source-volume \
  alsa_input.usb-Antlion_Audio_Antlion_USB_Microphone-00.mono-fallback
```

Before motion, require the board to be `IDLE`, `commissioningAxis` to be
`theta`, no stale recorder, no underruns or errors, valid theta UART, no driver
faults, and `settings.interpolationTo256: true`. Require
`persistenceAvailable: true` before treating an applied profile as saved. Keep
Antlion position, gain, orientation, sample rate, and PipeWire source fixed.
The tool fingerprints the capture gain before and after every recording and
rejects a changed setup from comparison.

If any preflight request times out, do not start motion. Confirm board power,
Wi-Fi reachability, and that the commissioning image is still installed. A
failed or interrupted trial always sends the stop endpoint and terminates its
recorder, but still verify `IDLE` before continuing.

If needed, stop and recover with:

```bash
pkill -INT -f '^python scripts/acoustic_tuner.py trial'
curl -fsS -X POST http://100.76.149.200/api/motion/stop
```

Never start a second trial until the first process is gone and telemetry again
reports `IDLE`.

## Firmware preparation

```bash
pio run -e esp32dev_theta_commissioning_ota -t upload
```

After reboot, wait for HTTP and repeat preflight. Commissioning firmware
intentionally forces a safe 250 mA, 64-microstep, 0.05 rad/s boot envelope even
when more aggressive tuning is saved. It also restores the full baseline
chopper, PWM, interpolation, hold-delay, and standstill profile. Runtime trials
apply requested values after the operator provides the motor rating.

## Calibrate the acceptable reference

1. Place the Antlion close without touching the frame or motor. Avoid airflow
   and mechanically fix its position.
2. Run the currently accepted full rotation and have the operator listen under
   the room conditions that matter. The current baseline uses 0.48 rad/s.
3. Record at least two complete idle -> motion -> idle passes.
4. Use the louder repeatable motion-locked line as `--tone-ceiling-dbfs`.
   Create the -3 dB and -6 dB ceilings by subtracting exactly 3 and 6.
5. Keep `nearHighSpeed.medianAWeightedDbfs` as a broadband safety check only
   when the pre/post idle windows are stable and no unrelated sound occurred.
6. Reject calibration if gain changed, clipping occurred, the microphone
   moved, or an unrelated sound obscured the motion-locked line. A knocked-mic
   run is wholly invalid; never salvage one half of it.

```bash
python scripts/acoustic_tuner.py trial \
  --label theta-reference-v0.48 \
  --rated-current-ma 1500 \
  --acceptable-ceiling-dbfs -53.92 \
  --tone-ceiling-dbfs -58.45 \
  --minimum-persistence 0.35 \
  --profile both --repeats 2 \
  --pre-idle 5 --duration 300 --post-idle 5 \
  --run-current-ma 700 --hold-current-ma 200 \
  --velocity 0.48 --accel 2 --jerk 10 \
  --microsteps 64 --mode stealthchop --coolstep off
```

The tool deliberately sets the Web UI speed control to 10/10 before a trial so
the measured velocity is the configured theta limit. Normal pattern playback
may use a lower speed multiplier; that does not change the saved limit.

## Apply and verify the selected profile

The tuning tool persists every requested settings update before starting
motion. To reapply the selected baseline without starting a test, use the Web
UI or these partial API updates while the board is `IDLE`:

```bash
curl -fsS -X POST http://100.76.149.200/api/tuning/motion \
  -d tMaxVelocity=0.48 -d tMaxAccel=2 -d tMaxJerk=10
curl -fsS -X POST http://100.76.149.200/api/tuning/theta \
  -d runCurrent=700 -d holdCurrent=200 -d microsteps=64 \
  -d stealthChopEnabled=true -d coolStepEnabled=false
curl -fsS http://100.76.149.200/api/tuning | jq \
  '{persistenceAvailable, motion, thetaDriver}'
curl -fsS http://100.76.149.200/api/tuning/dump/theta | jq \
  '{uartResponseValid, settings, status, globalStatus}'
```

Require successful POST responses, exact readback, valid UART, confirmed
interpolation to 256, and no fault flags. In theta commissioning mode, verify
the store immediately; do not reboot merely to test persistence because the
safe commissioning override is expected to change the runtime readback. Verify
the selected profile again after the eventual production boot and before the
first coupled-axis move.

## Fast tuning loop

Use one repetition for screening and at least two independent repetitions for
confirmation. `--duration` is a completion timeout, not a request to truncate
motion. Every finalist must finish both `continuous` and `stress` naturally.

### Short-screen, full-confirmation sequence

Do not spend a full rotation on every coarse parameter candidate. Use a short,
reversing angular screen first, with multiple complete motion/rest gates in one
continuous microphone recording. Each moving segment must be long enough for
telemetry to show at least 90% of the configured maximum velocity continuously
for at least one second. A short screen is invalid for acoustic comparison if
any segment fails that cruise requirement; increase the angular travel and
repeat it rather than comparing acceleration-dominated recordings.

A screening pass is provisional even when its microphone and operator checks
pass. Promote only the best candidates to this sequence:

1. repeat the short screen independently to reject room-noise coincidences;
2. run complete forward-and-back rotations to measure sustained sound across
   the mechanism's full position range;
3. run the full reversal-heavy stress profile;
4. confirm at least twice, then perform the five-repeat soak described below.

Every short or full sequence must end at its exact logical and physical start.
Correlate audio with every measured start and stop, compare each motion gate
with the idle immediately before and after it, and reject recordings with
missing gates, clipping, microphone-gain changes, unstable timing, or
unexplained background drift. The operator's audible verdict overrides a
numeric pass.

Theta commissioning provides a bounded segmented endpoint. The default screen
is `0 -> 90deg -> 0 -> 90deg -> 0`, and the gated qualification repeats that
out/back pair four times with an idle interval after every leg. Set
`--theta-excursion-deg` larger when telemetry cannot prove one second at 90% of
commanded velocity. The firmware accepts only targets from 0 through 2pi and
the host rejects any sequence that does not return to its exact start.

1. Establish one known pass.
2. Change only one family at a time: microsteps, current, velocity, or ramp.
3. Use coarse steps to find a bracket, then test midpoints.
4. Do not assume noise is monotonic. This mechanism has narrow resonances where
   a slightly faster or lower-current setting can be louder. Map neighboring
   points whenever results reverse direction.
5. Full rotations measure sustained-speed sound. Stress motion measures short
   moves, reversals, acceleration, and mechanism transients.
6. If full rotation passes but stress fails, tune acceleration/jerk at fixed
   speed. If stress passes but full rotation fails, tune speed, current, or
   microsteps.
7. Stop immediately when the operator reports hearing it. Human failure wins
   even if the microphone passes.

```bash
python scripts/acoustic_tuner.py trial \
  --label PROFILE-CANDIDATE \
  --rated-current-ma 1500 \
  --acceptable-ceiling-dbfs TIER_CEILING \
  --tone-ceiling-dbfs TONE_CEILING \
  --minimum-persistence 0.25 \
  --profile screen --theta-excursion-deg 90 --repeats 1 \
  --pre-idle 3 --gated-idle 2 --duration 120 --post-idle 3 \
  --run-current-ma CURRENT --hold-current-ma HOLD \
  --velocity VELOCITY --accel ACCEL --jerk JERK \
  --microsteps MICROSTEPS --mode stealthchop --coolstep off
```

Required search order (do not skip a family merely because the historical
baseline passed):

1. Compare 32, 64, and 128 external microsteps. Prefer lower CPU cost unless a
   higher value materially improves sound or visible smoothness. Confirm
   interpolation-to-256 remains enabled.
2. Find the lowest reliable loaded current, then examine nearby values. Current
   changes can move resonances rather than merely change volume.
3. Increase velocity with full rotations while retaining timing headroom.
4. Tune acceleration and jerk with stress motion. Test gentler and faster ramps
   because slow traversal can dwell in a resonance.
5. Keep StealthChop and automatic PWM calibration as the first quiet mode, then
   compare manual PWM derived from the observed automatic values.
6. Exercise the SpreadCycle chopper controls and a hybrid threshold at least
   once as controls; reject them quickly if the first screen is clearly louder.
7. Test the complete CoolStep parameter set after the best fixed-current
   profile exists. A CoolStep result is valid only when `CS_ACTUAL`, SG data,
   motion, and sound remain repeatable in both directions.
8. Tune hold current, hold delay, power-down delay, and standstill mode after
   motion passes. Score stop/idle transients separately from cruise.

### TMC2209 parameter matrix

Keep the motion profile and all other registers fixed while screening each
row. Run the short gated screen first, then promote the best setting to full
rotation and stress. Record the raw register dump with each artifact.

| Stage | Parameters | Initial sweep | Qualification rule |
|---|---|---|---|
| Current representation | `VSENSE`, `IRUN`, `IHOLD` | Compare standard and high-sensitivity ranges at the same nominal current; search current in coarse steps, then adjacent CS codes | Respect the motor rating after register quantization; prefer the range that represents the target with a higher CS code |
| Step resolution | `MRES`, `INTPOL` | 32, 64, 128 external microsteps; interpolation on/off | Keep at least 20% STEP-rate headroom; require exact CHOPCONF readback |
| StealthChop carrier | `PWM_FREQ` | 0, 1, 2, 3 | Compare motion-locked tones and broadband excess in both directions |
| StealthChop loop | `PWM_REG`, `PWM_LIM` | Coarse bracket across 1..15 and 0..15; test neighbors around the best pair | Reject weak torque, clipping, or unstable `PWM_SCALE` telemetry |
| Automatic tuning | `PWM_AUTOSCALE`, `PWM_AUTOGRAD` | on/on baseline, then controlled on/off combinations | Complete AT#1 at standstill and AT#2 at constant velocity before scoring; discard calibration motion from audio scoring |
| Manual PWM | `PWM_OFS`, `PWM_GRAD` | Start from `PWM_OFS_AUTO` and `PWM_GRAD_AUTO`; test small neighboring values with automatic adaptation disabled | Use only after the automatic profile is stable and recorded |
| SpreadCycle | `TOFF`, `TBL`, `HSTRT`, `HEND` | Start at 3, 2, 5, 0; sweep one field at a time within datasheet ranges | Reject `TOFF=1` with `TBL<2` and raw `HSTRT + HEND > 18`; use only in SpreadCycle or hybrid trials |
| Standstill | `IHOLD`, `IHOLDDELAY`, `TPOWERDOWN`, `FREEWHEEL` | Tune after motion sound passes; keep `TPOWERDOWN >= 12` | Score stop transients and idle separately; preserve enough hold torque for the mechanism |
| CoolStep | `SEMIN`, `SEMAX`, `SEUP`, `SEDN`, `TCOOLTHRS` | Keep off for the fixed-current baseline; test the complete set afterward | Accept only if current modulation stays repeatable and does not worsen tones or missed motion |
| Hybrid mode | `TPWMTHRS` | One controlled screen after SpreadCycle characterization; expand only if useful | Place the transition outside common operating resonances and test it in both directions |

The host exposes these controls as `--current-scale`, `--run-current-ma`,
`--hold-current-ma`, `--microsteps`, `--interpolation`, `--pwm-frequency`,
`--pwm-regulation`, `--pwm-limit`, `--automatic-current`,
`--automatic-gradient`, `--pwm-offset`, `--pwm-gradient`,
`--chopper-off-time`, `--blank-time`, `--hysteresis-start`,
`--hysteresis-end`, `--hold-delay`, `--power-down-delay`,
`--standstill-mode`, and the `--coolstep-*` options. The `--mode` and
`--stealth-threshold` options select StealthChop, SpreadCycle, or a hybrid
threshold. Keep double-edge stepping, shaft inversion, diagnostic routing,
and protection-disable bits fixed. Those fields change motion semantics or
disable protection instead of providing a valid sound-tuning variable.

Known boundaries with the current mechanism and fixed microphone:

- 0.40 rad/s only improved the line by about 1.6 dB; it is not a -3 dB tier.
- 0.325 rad/s hit a strong resonance at `-57.38 dBFS`.
- 0.30 rad/s passed one screen but failed confirmation at `-57.95 dBFS`.
- 0.225 rad/s is the fastest repeat-qualified -3 dB candidate.
- 0.20 rad/s failed the -6 dB ceiling at `-62.51 dBFS`.
- 0.15 rad/s is the fastest repeat-qualified -6 dB candidate.

Do not assume lower velocity is quieter; bracket around these resonances after
any mechanical change.

## Acceptance rules

A profile passes only when all are true:

- the operator did not hear objectionable motion or mechanism noise;
- every detected motion-locked line is at or below the selected tone ceiling;
- any broadband comparison used for rejection has stable pre/post idle windows
  and is free of unrelated room events;
- full rotation and stress both pass;
- every test completes naturally rather than reaching its timeout;
- requested velocity is reached where the path is long enough;
- theta UART and interpolation readback remain valid;
- there are no driver thermal, short, undervoltage, or communication faults;
- planner underruns and maximum consecutive underruns remain zero;
- rho stays stationary in theta commissioning mode;
- the board returns to `IDLE` after every trial.

Persistent-tone detection is the repeatable relative measurement for the
current room. Mechanism sound can also be broadband or transient, so inspect
valid high-speed A-weighted levels, motion-window peaks, timing plots, and the
operator report. Lines must be within 20 Hz-20 kHz, begin with motion, disappear
in both idle windows, and recur at the same frequency in repeated captures.
For bidirectional motion, a line that persists through one direction can be
real even if whole-window persistence is below 50%; compare the timeline and
both direction halves before accepting it.

`CS_ACTUAL` is a programmed current scale, not measured coil current.
StallGuard is a load/stall indicator, not a current meter. Without an encoder,
telemetry cannot prove the rotor followed every commanded step.

## Confirmation, soak, and handoff

Confirm each finalist with at least two full-rotation and two stress passes.
Then run the operator-selected profile for five of each:

```bash
python scripts/acoustic_tuner.py trial \
  --label theta-selected-loaded-soak \
  --rated-current-ma 1500 \
  --acceptable-ceiling-dbfs TIER_CEILING \
  --tone-ceiling-dbfs TONE_CEILING \
  --minimum-persistence 0.20 \
  --profile both --repeats 5 \
  --pre-idle 3 --duration 300 --post-idle 3 \
  --run-current-ma CURRENT --hold-current-ma HOLD \
  --velocity VELOCITY --accel ACCEL --jerk JERK \
  --microsteps MICROSTEPS --mode stealthchop --coolstep off
```

At completion, query tuning, driver dump, telemetry, errors, and status. Leave
the board `IDLE`. Run the acoustic self-test, Python compilation, native motion
tests, and `git diff --check`. Save finalist artifact paths and update long-term
project memory.

Raw WAV files, plots, timelines, and full result JSON are local lab data under
`tuning-recordings/`. That directory is intentionally ignored by Git. Commit
only compact settings, results, caveats, and decisions to this playbook.

When rho is connected, tune it separately, validate current-sensing homing with
human confirmation, and repeat coupled-axis motion, CPU, and acoustic tests.
