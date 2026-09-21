# Lighting presets at 10 kHz

> Historical: the PWM controls and diagnostic APIs described here were removed
> on September 19, 2026. See [current on/off light control](LIGHT_CONTROL.md).

The current control has five illuminated levels, **30%, 45%, 60%, 75%, and
100%, plus Off**. The user requested a wider, roughly even spread after reviewing
the narrow 88/91/94/97/100% candidates. The wider values are a practical control
choice, **not a validated flicker-free fix for the existing power circuit**.

The slider uses positions 0–5 and displays the actual requested percentage. API
brightness values remain percentages: normal requests accept only 0, 30, 45, 60,
75, or 100. Other percentages return HTTP 400. Corresponding eight-bit values are
0, 76, 114, 153, 191, and 255; full on maps to raw LEDC duty 256.

Manual changes apply immediately. Presence restoration also applies the selected
target immediately. Neither passes through intermediate fade values. The LED
controller independently rejects non-preset output values in normal mode, so
restricting the UI alone is not the only protection. Duplicate selections do not
rewrite the duty; failed driver writes do not publish a new target. Boot starts
with the light off. PWM is 10 kHz on GPIO4 with its software pull-down retained.

Continuous fading remains available only through explicit construction with
`usePresets=false`, for existing bench/unit tests. The lights-only diagnostic
image retains its explicitly requested `immediate=1` raw test bypass, and its
off-only frequency endpoint. Neither diagnostic endpoint bypass is available in
a production build.

## Optical check and limitation

At 10 kHz with fades bypassed, every directed transition among
0/30/45/60/75/100 was recorded once: 30 pairs, four-second holds, plus references.
Of 31 sequence intervals including the starting Off, 27 passed the earlier
settled optical screen. Four did not: one visit each to 30, 45, 60, and 75%.
The 45 and 60% failures showed substantial variation (approximately 41% and 20%
P95–P5 relative to net camera signal). Passing short intervals do not establish
long-term stability, nor do they establish a clean transition.

The earlier narrow candidate test completed the same 30-pair coverage for
0/88/91/94/97/100. Twenty-nine of 31 intervals passed; one visit each to 88 and
91% missed the screening thresholds. Those values were not installed because
the user requested broader spacing.

Artifacts: `/tmp/sisyphus-led-presets/wide-pairs/` and
`/tmp/sisyphus-led-presets/pairs/` include fixed-exposure video, original-resolution
ROI photometry with timestamps, microphone recordings, commands, and readbacks.
See [the exhaustive frequency report](LED_PWM_SWEEP_2026-09-17.md) for the
underlying converter concern, switching-loss uncertainty, and capture limits.

The [September 18 acoustic comparison](LED_ACOUSTICS_2026-09-18.md) tested
13 frequencies with these levels. None eliminated recorded tones and optical
instability across the entire slider. The quieter 25 kHz candidate still had
a 12.5 kHz tone and a large brightness excursion at 30% on retest, so the
installed default remains 10 kHz.

## Verification and deployment

Production and lights-only ESP32 builds pass. Arduino 2 and 3 ASan/UBSan host
tests cover all 256 input values, immediate preset changes, duplicate suppression,
write failures, presence restoration, diagnostic controls, legacy bench fades,
and signal observations. UI tests cover index/percentage mapping, labels,
firmware/UI list agreement, slow and reordered replies, retries, and polling.

The lights-only image is used for the connected lights-only bench: motor and SD
initialization remain disabled. Camera automatic controls are restored after
recording. The LED is left off after deployment verification.
