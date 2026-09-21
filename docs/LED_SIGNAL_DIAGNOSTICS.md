# LED signal diagnostics

> Historical: the PWM controls and diagnostic APIs described here were removed
> on September 19, 2026. See [current on/off light control](LIGHT_CONTROL.md).

`GET /api/led/brightness` reports the configured duty and frequency. It does
not measure the pin waveform. For flicker or unexpected darkness with correct
register readback, use `GET /api/led/signal` after motion and fades have stopped.

The signal endpoint temporarily enables GPIO4's digital input buffer and
samples the pad for approximately 20 ms. It restores the previous input-enable
bit and does not write the duty, timer, output routing, or pull-down settings.
The LED mutex prevents brightness changes during the observation. Interrupts
stay enabled, so scheduling can extend the observation or cause missed edges.
This is an on-demand diagnostic, not an endpoint for continuous UI polling.
It returns HTTP 409 if motion is active at admission, a fade is active, or the
LED controller is not ready.

At the diagnostic 1 kHz setting and 80% requested brightness, duty is 204/256.
An undisturbed signal should produce approximately:

| Field | Expected |
| --- | --- |
| `sampleDurationUs` | 20,000 |
| `sampledHighPercent` | 79.7 |
| `risingEdges`, `fallingEdges` | about 20 each |
| `minHighUs`, `maxHighUs` | 797 |
| `minLowUs`, `maxLowUs` | 203 |
| `minPeriodUs`, `maxPeriodUs` | 1,000 |

Check `maxSampleGapUs` before interpreting pulse widths. A sampling gap near a
pulse's duration can miss that pulse entirely or make it appear too long.
Repeat noisy measurements; software polling is not an oscilloscope. Pulse
range fields are zero when no complete pulse/period was observed. Constant
off/on output should have no edges and approximately 0/100% sampled high time.

A good sample at the ESP32 pin does not measure the gate voltage after the
220-ohm resistor, drain voltage, LED current, or 12 V supply. Conversely, a
stuck or irregular sampled output warrants further GPIO/LEDC investigation.

## Software audit and validation

The September 17, 2026 audit found no second LEDC user, GPIO4 reconfiguration,
or overlap between LEDC and the motion timer-group peripherals. Dynamic
frequency scaling is disabled in the installed Arduino 2 SDK configuration.
The controller serializes writes and diagnostics with the LED mutex. Camera
A/B testing earlier reproduced the blackout and flicker with manual fades
bypassed. The user subsequently isolated the lights, added another shared ground
path, and authorized camera measurements again; the fault persisted.

Host tests cover normal PWM, stuck-low/high signals, missing pulses, clock
wrap, scheduling gaps, restoration of both initial input-enable states, and
unchanged duty/target/write count. Existing fade and presence tests also pass
on the Arduino 2 and 3 code branches. The production ESP32 build passes.
Hardware validation now reproduces the fault with motor and SD initialization
disabled (`esp32dev_led_diagnostic_ota`). This environment retains OTA, normal
CPU frequency, and normal radio settings. Motion remains locked out. The signal
endpoint also works in this build's deliberately uninitialized motor state.

During two settled 80% intervals, seven samples with a maximum polling gap below
50 us showed 20 rising edges per 20 ms, periods of 999–1,001 us, high pulses of
795–797 us, and sampled high time of 79.5–79.6%. PWM duty remained 204 and the
write count stopped changing. Simultaneous fixed-exposure camera recordings
still showed the light fluctuating. At 100%, the pin was continuously high and
the light was steady. Longer scheduling gaps were reported and excluded from
pulse-width conclusions.

A second camera/pad test bypassed fading entirely. Each brightness command
caused one PWM write. Both 100-to-80% transitions still produced approximately
0.7–0.8 seconds of darkness, with clean 1 kHz / 79.6–79.7% GPIO samples during
the dark intervals. Camera and command clocks were approximately aligned using
the first instantaneous switch to 100%; these are not oscilloscope-synchronized
measurements. Artifacts are in `/tmp/sisyphus-led-isolated/immediate-signal-1khz/`.
The normal fade source and lights-only fading firmware were restored afterward,
and the camera's automatic exposure, white balance, and focus were restored.

These observations point downstream of the digital GPIO output; they do not
identify the faulty component or establish the analog gate voltage. The user
reports a 20 V USB-C supply stepped down to 12 V. Converter output stability,
the MOSFET gate drive, and the power wiring still require electrical checks.
The user subsequently identified the converter chip marking as GME1C, consistent
with RY8336, and confirmed MOSFET marking IRLZ44N P525G B9N0.

## Frequency comparison and later measurements

User measurements at 100%: converter output 12 V, USB-C input 20 V / 1.27 A,
MOSFET drain/source drop 65 mV. At 76%, converter output was 10.74 V and input
approximately 19.55 V. At 94%, converter output was reported as 9.9 V. Unloaded
converter input/output were approximately 19.74/12.24 V. The loaded output sag
with ample DC input headroom points to the converter stage/output power path;
a meter cannot exclude fast input transients. See `LED_MOSFET_LOSSES.md` for
the conduction estimate and switching-loss limitations.

A temporary 10 kHz lights-only firmware was tested with the camera, then the
1 kHz source and firmware were restored. The user added a heatsink to the buck
module during the first comparison and also changed brightness manually;
that first 1 kHz sequence is not a controlled baseline. A repeated 1 kHz
recording after heatsink installation still showed blackouts and fluctuations
at 80% and 94%. A repeated 10 kHz recording had much steadier later intervals,
but substantial flashing in its initial portion. The user also reported
audible noise and flashing during the test/restoration sequence. Exact camera
versus HTTP timing is approximate; do not infer precise transition latencies
from the plots or assign the audible event to a specific frequency without
further evidence. This is promising frequency sensitivity, not a validated
complete fix or evidence of acceptable long-term switching loss.

Artifacts: `/tmp/sisyphus-led-frequency/` includes recordings, telemetry,
photometry, temporary image, and restore logs. After the report of noise and
flashing, tests were stopped, firmware restored to 1 kHz, and LED duty/target
verified zero. Camera automatic exposure, white balance, and focus were restored.

Session artifacts: `/tmp/sisyphus-led-isolated/signal-1khz/` contains the camera
recording, GPIO samples and brightness telemetry (`telemetry.json`), and
photometry. Camera intensity values are uncalibrated; compare stability within
the recording rather than treating them as linear light-output measurements.

## Reproducible frequency sweep

The lights-only diagnostic build boots with its light off. In that build only,
`POST /api/led/frequency` accepts a form field `frequencyHz` with one of
1000–25000 Hz in 500 Hz increments. This bounded range supports the
September 18 microphone comparison without a flash for every new candidate.
Invalid values return 400. Changing frequency
requires zero applied and target brightness with neither manual nor presence
fading active; otherwise it returns 409. Frequency selection is temporary and
does not persist through reboot. Production builds do not expose this route.

The diagnostic brightness endpoint also accepts `immediate=1` to bypass the
manual fade for a single request. It updates applied and target brightness
together after a successful PWM write. The runner's `--immediate` flag uses it.
Production builds ignore this test-only parameter and enforce the selected presets.
Normal lighting now uses 10 kHz and direct preset changes; see [LED presets](LED_PRESETS.md).

The [September 17 exhaustive sweep report](LED_PWM_SWEEP_2026-09-17.md) records
the 404-point frequency screen, longer retests, transition failures, and final
restored state. It did not validate a reliable five-level preset replacement.

`scripts/led_frequency_sweep.py` records camera video, per-frame photometry and
monotonic acquisition timestamps, microphone audio, commands, and PWM readback.
It requires Python numpy/OpenCV, ALSA `arecord`, `amixer`, and `v4l2-ctl`.
Set fixed camera exposure, gain, white balance, and focus before running. The
default camera ROI is specific to this bench view and must be reviewed before
using a different placement. The microphone is the Antlion USB microphone.

Example (output directory must not already exist):

```sh
python scripts/led_frequency_sweep.py --output /tmp/led-sweep --hold 4
```

The default sequence tests all integer percentages 0–100 in the same shuffled
order at each frequency, with off/full baselines before and after each sweep.
Each four-second interval retains the normal one-second firmware fade; summary
statistics exclude its first 1.5 seconds. Use `--frequencies 5000` and
`--levels 0,20,40,60,80,100 --hold 30` for longer targeted retests. The script
checks actual duty/frequency, write errors, camera continuity and microphone
process health. It requests and verifies lights off in its exit cleanup.

Video indices map to `frames.csv` rows. Camera timestamps are acquisition times,
not calibrated exposure timestamps; the capture uses a one-frame buffer. Audio
has a recorded subprocess-start timestamp, not hardware synchronization with
the camera. Compare settled segments with margin around transitions. This
30 fps camera detects the slow flashing seen on this bench, but cannot certify
absence of fast optical modulation. Uncalibrated camera values and microphone
levels do not measure electrical losses, absolute luminous output, sound
pressure, component temperature, or ultrasonic noise above the audio bandwidth.
