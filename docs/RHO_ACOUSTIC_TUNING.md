# Rho commissioning and acoustic tuning playbook

Use this playbook to select quiet, reliable settings for the paired rho motors
before tuning sensorless homing. It uses the same Antlion USB microphone and
the telemetry-aligned analysis in `scripts/acoustic_tuner.py`.

This procedure deliberately does **not** home rho. The operator places the
mechanism about 30 mm outward from the physical home stop before commissioning
firmware boots. Firmware assigns that position temporary logical `rho=0`.
Every test target is between logical 0 and +200 mm, and every successfully
completed test returns to logical 0. No test commands an inward-negative rho
position.

## Test trajectories

- Continuous: `start -> start+200 mm -> start`.
- Stress: reversal-heavy moves through offsets from 0 to +200 mm, followed by
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
`esp32dev_rho_commissioning_ota`. This build:

- disables automatic and manual homing;
- disables manual moves, patterns, clearing, and theta tuning tests;
- explicitly disables the theta driver and verifies `TOFF=0` over UART, which
  also makes warm OTA transitions safe;
- enables only the paired rho drivers;
- boots with a conservative 150 mA run / 100 mA hold, 2 external microsteps,
  StealthChop, CoolStep off, 1 mm/s velocity, 2 mm/s² acceleration, and
  10 mm/s³ jerk;
- assumes the physical boot position is temporary logical rho zero;
- permits only the bounded rho continuous and stress generators.

A trial may apply different settings after the operator supplies the motor's
rated RMS phase current. Firmware independently enforces the project rho
ceiling of 500 mA. The 500 mA ceiling is not a tuning target.

Do not use the general bench-motion build for this procedure. It does not
provide the rho-only motion lockout or the host-side commissioning identity
check.

## Hard safety rules

1. With driver power off, put the mechanism at the marked test start about
   30 mm outward from the physical home stop. Confirm at least 200 mm of clear
   outward travel remains.
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

## Establish a new rho acoustic reference

First record stationary room/mechanism noise:

```bash
python scripts/acoustic_tuner.py baseline \
  --duration 10 --settle 3
```

For the first observed movement, use a conservative continuous-only screen.
Replace `RATED_CURRENT` with the lower of the motor's rated RMS phase current
and 500 mA. The 600-second timeout allows the 400 mm round trip at 1 mm/s to
finish naturally.

```bash
python scripts/acoustic_tuner.py trial \
  --axis rho --reference-only \
  --label rho-smoke-2u-150ma-v1-a2-j10 \
  --rated-current-ma RATED_CURRENT \
  --profile continuous --repeats 1 \
  --pre-idle 5 --duration 600 --post-idle 5 \
  --run-current-ma 150 --hold-current-ma 100 \
  --velocity 1 --accel 2 --jerk 10 \
  --microsteps 2 --mode stealthchop --coolstep off
```

If 150 mA cannot move both motors cleanly, stop, restore the physical start,
reboot, and increase current in bounded steps within the motor rating. Do not
interpret a stalled low-current run as an acoustic candidate.

Once motion is visibly healthy, record at least two continuous and two stress
repeats using the current known-reliable profile and `--reference-only`. This
creates rho-specific broadband and timing-locked tonal references:

```bash
python scripts/acoustic_tuner.py trial \
  --axis rho --reference-only \
  --label rho-reference \
  --rated-current-ma RATED_CURRENT \
  --profile both --repeats 2 \
  --pre-idle 5 --duration DURATION --post-idle 5 \
  --run-current-ma CURRENT --hold-current-ma HOLD \
  --velocity VELOCITY --accel ACCEL --jerk JERK \
  --microsteps MICROSTEPS --mode stealthchop --coolstep off
```

Select the louder repeatable motion-locked line as the baseline tone ceiling.
Use high-speed A-weighted dBFS only as a secondary broadband ceiling when both
idle windows are stable. All readings are relative digital dBFS, not SPL.

`--duration` is a per-sequence safety timeout, not the recording duration. The
continuous sequence travels 400 mm total; the stress sequence travels 2,850 mm
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
  --minimum-persistence 0.35 \
  --profile both --repeats 1 \
  --pre-idle 5 --duration DURATION --post-idle 5 \
  --run-current-ma CURRENT --hold-current-ma HOLD \
  --velocity VELOCITY --accel ACCEL --jerk JERK \
  --microsteps MICROSTEPS --mode stealthchop --coolstep off
```

The tool sets the UI speed multiplier to 10/10, persists requested values,
records one continuous Antlion file, polls motion telemetry, samples both rho
driver dumps, and writes WAV, timeline JSON, timing plot, detailed result JSON,
and append-only `results.jsonl` artifacts under `tuning-recordings/`.

## Search order

Change one family at a time:

1. **Reliable current:** find the lowest current that completes both directions
   and stress without missed motion. Check nearby values because current moves
   resonances rather than simply changing volume.
2. **External microsteps:** compare 2, 4, 8, and 16 first. Higher values sharply
   reduce the available velocity under the 10 kHz step budget. Require
   interpolation-to-256 readback on both drivers.
3. **Velocity:** bracket upward and downward around repeatable tonal peaks. Do
   not assume slower is quieter. Use continuous runs to judge sustained sound.
4. **Acceleration and jerk:** tune with the stress profile at fixed current,
   microsteps, and velocity. A gentler ramp can dwell in a resonance; test both
   sides of any apparent improvement.
5. Keep StealthChop and automatic PWM calibration as the first quiet mode.
   Keep CoolStep off until fixed-current behavior is repeatable. Test
   SpreadCycle or hybrid thresholds only if torque/stability requires it.

Use one repeat for screening, two independent repeats for confirmation, and
five continuous plus five stress repeats for the selected soak profile.

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
- any broadband rejection used stable pre/post idle windows;
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
