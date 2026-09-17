# Selected sound-tuned settings

Accepted settings as of 2026-09-16 for the assembled table with theta and
main RHO connected. The counterweight motor is absent and its driver stays
disabled. These are the selected settings from the tuning work; the evidence
below describes what was measured and where qualification remains incomplete.

## Motion and current

| Setting | Theta | RHO |
|---|---:|---:|
| Maximum velocity | 0.225 rad/s | 5.5 mm/s |
| Maximum acceleration | 2 rad/s² | 20 mm/s² |
| Maximum jerk | 10 rad/s³ | 100 mm/s³ |
| Run current request | 700 mA | 350 mA |
| Hold current request | 200 mA | **200 mA** |
| External microsteps | 16 | 8 |
| Interpolation to 256 | Enabled | Enabled |

RHO hold was reduced from 350 to 200 mA through `POST /api/tuning/rho` and
saved on the device. The operator accepted 200 mA for this documented profile.
Settled live current-scale readback changed from CS10 to CS6, with healthy
UART communication, no driver faults, and no commanded movement. Run current
remained at CS10. No new acoustic recording or independent holding-torque
measurement accompanied this change.

Current values are firmware requests, not measured coil RMS currents. Theta
uses fixed PWM with automatic current scaling disabled; its current requests
do not establish an enforced RMS current. RHO uses regulated current: with
the configured 0.11-ohm shunts, CS10 and CS6 correspond to approximately
337 and 214 mA nominal, respectively.

## Driver settings

| Setting / API field | Theta | RHO |
|---|---:|---:|
| `stealthChopEnabled` | true | true |
| `stealthChopThreshold` | 0 (always) | 0 (always) |
| `highSensitivityCurrentScale` | false | true |
| `chopperOffTime` / `hysteresisStart` / `hysteresisEnd` | 5 / 5 / 0 | 3 / 5 / 0 |
| `blankTime` | 2 | 0 |
| `pwmFrequency` | 1 | 2 |
| `pwmRegulation` / `pwmLimit` | 1 / 12 | 1 / 8 |
| `pwmOffset` / `pwmGradient` | 76 / 23 | 128 / 0 |
| `automaticCurrentScaling` | false | true |
| `automaticGradientAdaptation` | false | false |
| `coolStepEnabled` | false | true |
| `coolStepLowerThreshold` / `coolStepUpperThreshold` | 5 / 2 (inactive) | 2 / 1 |
| `coolStepCurrentIncrement` / `coolStepMeasurementCount` | 1 / 1 (inactive) | 2 / 0 |
| `coolStepThreshold` | 2000 (inactive) | 1000 |
| `holdDelay` | 8 | 8 |
| `powerDownDelay` | 20 | 20 |
| `standstillMode` | 0 | 0 |

Delay, frequency, and chopper values above are register settings, not durations
or frequencies in physical units. Keep the full profile together when using
the measured tuning results.

## Evidence and limits

- **Theta:** the selected 0.225 rad/s profile measured a conservative
  sustained-motion upper bound of -65.59 dBFS. The sampled search also qualified
  0.23125 rad/s for the -63 through -65 tiers and 0.548125 rad/s for the -58
  through -62 tiers. The selected speed gives more margin at the strict tier.
  Ramp/reversal transients are outside those sustained-motion claims; the
  faster profile also has an intermittent-tone listening caveat. See the
  [loaded theta results](THETA_ACOUSTIC_TUNING.md#loaded-retune-result).
- **RHO:** 5.5 mm/s passed two 25 mm out/back short screens with a loudest
  whole-STEP window of -60.83 dBFS and sustained incremental upper bound of
  -67.86 dBFS, using 350 mA run and hold. Subsequent background-contaminated
  rechecks were inconclusive. The operator adopted this motion profile;
  full-travel/stress acoustic qualification and the quieter tiers remain
  incomplete. Accepting 200 mA hold does not convert those earlier recordings
  into measurements of the new hold setting.
- **Idle sound:** earlier stationary 350/100/350 mA testing did not demonstrate
  a reversible sound reduction. Separate PWM-frequency comparisons identified
  hold tones at settings 0 and 1; the selected RHO frequency is 2. Lower current
  or lower speed did not consistently mean quieter operation.
- **Historical RHO:** the paired-mechanism 150/75 mA fixed-PWM results are not
  qualifications for this main-only regulated profile. Later reversal stress
  exposed synchronization loss. Preserve those records as historical evidence.

The microphone is no longer available. Numerical dBFS results belong to their
recorded microphone geometry, gain, load, and test conditions; they are not
room-independent sound-pressure ratings or proof of inaudibility. Detailed
trials and rejected candidates remain in the
[RHO acoustic playbook](RHO_ACOUSTIC_TUNING.md).

## Persistence and homing

`GET /api/tuning` is the authority for saved runtime settings. The device now
stores RHO 350 mA run / 200 mA hold. Firmware fallback still uses 350/350 mA;
this documentation update does not change that constructor default. A normal
production boot loads valid saved tuning, while missing or invalid tuning
falls back to the compiled profile. Commissioning images use separate startup
overrides and must not be mistaken for the production profile.

RHO homing keeps its independent 500 mA run/hold request, 12 mm/s speed,
8 interpolated microsteps, CoolStep disabled, and three-contact agreement
within 0.4 mm. Automatic production boot homing remains enabled. The 200 mA
hold setting applies to normal operation, not the dedicated homing sequence.
See the [homing playbook](RHO_HOMING_TUNING.md) for qualification and bounds.
