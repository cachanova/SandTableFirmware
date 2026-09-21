# Selected sound-tuned settings

## Full automatic image with optional CW (2026-09-20)

The current requested image is `esp32dev_full_auto` (USB) or
`esp32dev_full_auto_ota` (Wi-Fi). It includes theta, main RHO, and automatic
detection of the optional CW driver. Automatic boot homing is enabled again.
With the CW socket empty, boot and manual Home home main RHO only; theta,
main RHO jogging, and normal motion after homing remain available. CW controls
and its position cursor remain unavailable until its driver is detected.

After installing CW with power off, the next startup detects and configures it
with the shared RHO tuning and includes it in homing, CW first then main.
Driver participation is fixed for that boot: losing communication with a
previously detected driver during homing remains a failure. Missing CW does
not require disable verification, phase holding, or profile writes to its empty
socket. A detected driver whose configuration fails is not treated as absent.

The manual build below remains available for debugging without boot homing.
All these full builds use the same sound and dedicated homing profiles.

## Full manual image (2026-09-20)

The operator requested the full application on the replacement ESP32. Use
`esp32dev_full_manual` (USB) or `esp32dev_full_manual_ota` (Wi-Fi). These builds
enable theta, main RHO, counterweight RHO, SD/pattern storage, lights, and
presence sensing, with the adopted profiles below. Automatic homing is off;
boot leaves the position unconfirmed and permits relative jogs on connected
axes. RHO's fallback hold request is 200 mA, matching the accepted saved value.

Press Home from any stationary starting position to run sensorless homing;
no manual positioning, Set Home, or pre-confirmed zero is required. The
counterweight, when detected, homes first, then main RHO, using the same dedicated profile
and unknown-position entry (1 mm inward probe, 6 mm outward runway). Each
motor must obtain three agreeing contacts; travel limits, UART checks, the
90-second cycle deadline, and Abort homing remain active. Successful homing
establishes zero and enables normal pattern/absolute motion. This reuses the
main-RHO settings for the counterweight at the operator's request; physical
paired qualification is still pending. Boot itself never starts homing.

### Independent jogging and CW direction

The operator requested reversing CW after idle snapshots showed main DIR low
and CW DIR high. `kRhoCompanionDirectionInverted = true` applies that reversal
through the CW driver's SHAFT bit during boot, profile changes, homing, and
recovery; main RHO is unchanged. Those snapshots verified the configured
inversion only: they do not prove that either DIR input follows In/Out commands
or that the board intentionally inverts CW DIR. The subsequent report that
In/Out does not reverse motion requires a dynamic GPIO25-to-driver-input check.
A fixed SHAFT setting cannot repair a stuck DIR signal.

Read-only driver dumps now include `controller.dirPin`, `dirOutputLatch`,
`dirOutputEnabled`, `dirMatrixSignal`, and `dirMatrixInverted`, alongside
TMC `inputs.direction`. The controller latch is not an electrical pad-voltage
measurement. Compare In versus Out at rest after bounded jogs; a toggling latch
with a fixed driver input narrows the fault to the output pad/interconnect/driver
input and needs an electrical measurement to distinguish them.

Driver dumps also attempt a live read when startup detection failed. Full dumps
include `availableAtBoot` and a `uartProbe` with request-echo, reply-length,
framing, and CRC evidence from the last IOIN read attempt. If the local echo
is missing or mismatched, the reply stage was not reached. These reads do not
configure drivers or change motion availability.

The Manual page offers paired RHO, main-only, and CW-only ±1/10/100 mm jogs.
An independent jog isolates the other driver from shared STEP pulses while
holding its phase at u256 with a serviced low-speed internal generator.
Completion, Stop, and Abort restore its normal interface; communication or
phase-restoration failures disable RHO and require profile recovery. Independent
jogs invalidate paired homing, but further relative jogs and Home remain usable.

A faded grey cursor tracks CW at theta + 180 degrees, independently of the
main cursor. Both estimates follow executed planner movement rather than queued
targets and ignore logical recentering between unhomed jogs. Before a reference
exists, both start at a display midpoint (212.5 mm), explicitly labelled as a
relative estimate. Successful Home or Set Home establishes zero. Homing clears
untrusted estimates until success or another relative jog. These are commanded
step estimates, not encoders: skipped steps and external movement are not sensed.

The second reported homing attempt (inward travel succeeded, outward return
struggled) could not be recovered: the device had power-cycled before retrieval,
leaving homing cycle 0 and an empty trace. No homing detector/current/speed changes
were justified by that missing log; tuning remains unchanged.

## Counterweight reinstalled: manual service build (2026-09-20)

Use `esp32dev_rho_paired_service_ota` for the reinstalled counterweight. Both
RHO drivers receive the saved main-RHO motion/driver settings below and the
same dedicated homing profile. Unlike the older commissioning image, this
build retains saved tuning on reboot; without saved tuning it uses the adopted
profile with 350 mA run / 200 mA hold. It boots unhomed in manual relative-jog
mode, with automatic homing disabled. Theta, patterns, and SD access remain
disabled in this RHO service image.

Manual jogs are available immediately. Confirm both RHO mechanisms physically
at home before selecting Commissioning and starting a bounded Home test;
known-position homing also accepts separate measured starting distances.
The service sequence retains its 8 mm outward entry, followed by the shared
500 mA / 12 mm/s homing profile and saved detector/backoff settings. It homes
the counterweight first, then main RHO, and requires physical review afterward.
The main-only production startup entry is not enabled for paired operation.
Counterweight sound and homing qualification are deferred to later tuning.

## Main-only accepted profile

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
