# PWM whine comparison — September 18, 2026

> Historical: the PWM controls and diagnostic APIs described here were removed
> on September 19, 2026. See [current on/off light control](LIGHT_CONTROL.md).

The user reported high-pitched noise with the installed 10 kHz presets and
requested microphone testing at other frequencies, retaining Off, 30%, 45%,
60%, 75%, and 100%. Normal brightness changes remain instantaneous.

## Capture and analysis

The first recording compares 10, 25, 5, and 2.5 kHz. Each frequency has six
seconds off, eight seconds full on, six-second holds at
30/45/60/75/100/75/60/45/30/0%, then eight seconds full and six seconds off.
The second recording adds 12.5, 16, 20, 22, and 24 kHz, using the same sequence
with four-second test holds. Frequency changes occur only with output/target
off. API readback verifies the duty, frequency (within 0.1% divider rounding),
and zero PWM write errors.

A third recording refines the search at 24.5, 23.5, 23, and 21 kHz, with the
same four-second up/down sequence. The diagnostic-only endpoint now accepts
1–25 kHz in 500 Hz increments, using one shared validator for controller and
HTTP requests. The off-only interlock remains enforced. The production default
remains 10 kHz; none of these candidates qualified as a reliable replacement.

Audio: Antlion USB microphone, mono 48 kHz signed 16-bit, fixed capture gain
15 dB. Camera: Logitech C925e, 1280×720 at 30 fps, manual approximately 20 ms
exposure, gain 150, fixed white balance and focus, reflected-light ROI
x=500–1100/y=120–480. Focus was locked during the opening reference portion
of the first recording; later test holds use fixed controls.

`scripts/analyze_led_acoustics.py` excludes the first 1.5 seconds and last
0.2 seconds of each hold. Median Welch spectra use 16,384-sample windows
(approximately 2.93 Hz spacing). A provisional tone flag requires a 2–20 kHz
peak at least 8 dB above the maximum light-off reference spectrum, 8 dB above
its local approximately 300 Hz median, and at least −100 dBFS/Hz. These
thresholds identify conspicuous recorded tones; they are not hearing thresholds.
Full-on records are analyzed too, rather than assumed acoustically silent.

Room noise changed substantially, particularly at low frequencies. Narrow
tones that recur with brightness and move with PWM are more useful evidence
than total microphone RMS. No sound-pressure calibration, ultrasound
measurement, MOSFET switching-loss measurement, or temperature measurement
was performed. Passing the acoustic screen does not certify inaudibility.

Optical screening retains the earlier criteria: settled P95–P5 below 2% and
individual excursions below 5% of net camera signal. Room-light drift can
affect borderline results, especially at low brightness. The camera cannot
resolve the PWM carrier; it detects slower brightness fluctuations.

Artifacts are under `.pio/led-acoustics-2026-09-18/`, with raw microphone WAV,
camera video, frame photometry, command/readback logs, and analysis JSON.
`/tmp/sisyphus-led-acoustics/` is a compatibility symlink to that directory.

## Initial observations

At 10 kHz, all eight dimmed holds produced tone flags. At 75%, a 10 kHz tone
reached approximately −75 dBFS/Hz, about 35 dB above its light-off reference.
Lower presets also produced strong 5 and 15 kHz components. At 25 kHz, only
one of eight dimmed holds crossed the acoustic thresholds: a much weaker
approximately 12.5 kHz component at 30% (−98 dBFS/Hz). This is quieter in
this recording, not proof of silence. The 2.5 and 5 kHz recordings also
flagged tones at all eight dimmed holds.

None of these first four frequencies passed the optical screen at every
brightness. Notably, 25 kHz still produced an approximately 20% excursion at
30%. Frequency changes alone have not established a stable power circuit.

The intermediate recording found persistent 6.25 kHz components at 12.5 kHz
PWM, 5.33 kHz components at 16 kHz PWM, 10 kHz components at 20 kHz PWM,
11 kHz components at 22 kHz PWM, and 12 kHz components at 24 kHz PWM.
The 22 and 24 kHz carriers therefore do not imply an absence of audible-band
tones. The observed frequencies are consistent with subharmonic operation;
the microphone does not identify which physical component produces them.

## Thirteen-frequency screen

Each frequency includes eight dimmed holds (two visits to each of
30/45/60/75%). Off and full-on are excluded from the pass counts below.
The tone column identifies a repeatable PWM-related component rather than
assigning every changing room-noise peak to the circuit. PSD values are
uncalibrated microphone dBFS/Hz, not SPL or perceived loudness.

| PWM (kHz) | Notable tone (kHz) | Recorded peak (dBFS/Hz) | Optical passes / 8 |
| ---: | ---: | ---: | ---: |
| 2.5 | 5 | −82 | 3 |
| 5 | 5 | −82 | 1 |
| 10 | 10 | −75 | 4 |
| 12.5 | 6.25 | −84 | 0 |
| 16 | 5.33 | −80 | 4 |
| 20 | 10 | −84 | 1 |
| 21 | 10.5 | −84 | 4 |
| 22 | 11 | −79 | 4 |
| 23 | 11.5 | −82 | 2 |
| 23.5 | 11.75 | −88 | 2 |
| 24 | 12 | −82 | 4 |
| 24.5 | 12.25 | −83 | 4 |
| 25 | 12.5 | −98 | 3 |

The first 25 kHz screen had the weakest prominent circuit-related tone.
No tested frequency established both quiet operation and stable light across
the selected slider levels. In particular, 20 kHz had a large approximately
67% downward excursion at 75%; 23 and 23.5 kHz also showed substantial drops.
PWM duty/frequency readback remained correct with zero driver-write errors.

Background interference was especially strong during the first half of the
24.5 kHz capture, including flags at full on. Those broad/changing peaks
cannot reliably be attributed to the circuit. The 12.25 kHz tone recurred
at the later 30% hold after that interference diminished.

## Longer 25 kHz retest

The `retest-25k` capture uses eight-second holds at
30/0/45/0/60/0/75/0/100/0/30/0%, with the usual initial/final references.
Fresh light-off intervals separate the levels to help distinguish whine from
changing room noise. At the first 30% hold, the approximately 12.5 kHz tone
reached −93 dBFS/Hz, roughly 18 dB below the strongest 10 kHz tone in the
initial recording. This is a comparison of recorded spectral peaks, not a
calibrated loudness reduction.

That same hold had approximately 48% P95–P5 variation and a 57% downward
excursion relative to net median light. The 45 and 60% holds also failed the
optical screen. Therefore 25 kHz is a quieter candidate, **not a validated
quiet, flicker-free setting**. A narrow tone remains detectable at 30%.

The second 30% hold again produced the 12.5 kHz tone, this time approximately
−89 dBFS/Hz, and a 25% downward light excursion. Only the completed 75 and
100% illuminated retest holds passed the optical screen. The two 30% holds
triggered acoustic flags; 45/60/75/100% did not cross the acoustic thresholds
in this retest, which does not prove they are inaudible under all conditions.

The recorder stopped approximately 108.6 seconds into the retest because the
`/tmp` user disk quota was exhausted, during the last off hold. The final
full/off reference pair was not recorded. All illuminated holds had completed
by approximately 104.4 seconds; the saved WAV covers them (108.125 seconds
of audio). Analyses use only completed intervals, with several earlier
interleaved off references. The failed final interval is excluded. The final
video may be truncated. Captures were moved to the ignored `.pio/` artifact
directory to preserve them and free temporary storage; analysis was rerun
successfully afterward. No capture was silently counted as complete.

## Software validation and final state

The bounded diagnostic frequency validator passed Arduino 2 and Arduino 3
ASan/UBSan host tests, including every 500 Hz increment, invalid inputs,
off-only restrictions, and write-failure behavior. Production and lights-only
ESP32 builds passed. The acoustic analyzer passed a synthetic test detecting
an injected 10 kHz tone while rejecting a shared 7 kHz background tone.

The installed lights-only diagnostic firmware retains the five illuminated
presets and immediate changes. Its additional diagnostic frequencies are
temporary runtime selections; startup remains 10 kHz. Firmware SHA-256:
`4d5098a21f629525806ba89c969be534e85e73d46c3606544ea47b56fe8872c5`.
No candidate was made the permanent default. Final cleanup verified 10 kHz,
zero duty/target, zero PWM errors, and restored camera automatic controls. Motor/SD hardware
remains disabled for the lights-only bench.
