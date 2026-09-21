# LED MOSFET loss analysis

The user requested a parallel review of the directly driven LED MOSFET on
September 17, 2026. A subsequent drain/source measurement supports a conduction
loss estimate; switching loss remains unmeasured.

## Circuit and observations

- GPIO4 drives the gate through 220 ohms, nominally at 3.3 V.
- Source is common ground; drain connects to LED negative; LED positive is 12 V.
- The user confirmed the physical marking: `IRLZ44N P525G B9N0`.
- At 100%, the converter maintains 12 V and USB-C input is 20 V / 1.27 A.
  That is 25.4 W total input, implying at most 2.12 A at 12 V before losses.
  Approximately 2 A is an illustrative LED current, not a direct measurement.
- Converter output falls under PWM: reported 10.74 V at 76% and 9.9 V at 94%.
  Loaded converter input is approximately 19.55 V, versus 19.74 V unloaded.
- Digital GPIO sampling and no-fade camera tests show correct PWM while the
  light blacks out or fluctuates. These do not measure analog gate voltage.

## Gate-voltage limits

The [Infineon IRLZ44N datasheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irlz44n-datasheet-en.pdf)
specifies maximum on-resistance of 35 milliohms at 4 V, 25 milliohms at 5 V,
and 22 milliohms at 10 V. It does not guarantee resistance at 3.3 V.
Threshold voltage is not the voltage required for full enhancement.
Its gate-charge test conditions also differ substantially from this circuit.

The physical marking resolves the earlier uncertainty with IRFZ44N; the device
under test is the logic-level IRLZ44N.

## Measured conduction estimate

In response to the request for a steady 100% measurement, the user reported
65 mV directly between drain and source. At the inferred 1.9–2.0 A current,
this corresponds to approximately **0.124–0.130 W** conduction loss and
**32.5–34 milliohms** effective on-resistance. Even the lossless input-power
current ceiling of 2.117 A gives only 0.138 W. This particular device appears
adequately enhanced for the observed steady load. This is not a guarantee for
all IRLZ44N devices at 3.3 V or a measurement of switching loss.

The typical output curves are consistent with resistance in the tens of
milliohms at this current, but their pulse-test conditions and limited graph
resolution make the direct 65 mV reading more useful. Gate/source voltage and
switching waveforms are still needed.

## Conduction sensitivity

For an assumed constant 2 A during the on-period, `P = I² × R × duty`:

| Assumed actual on-resistance | At 76% | At 94% |
| --- | ---: | ---: |
| 0.035 ohm | 0.106 W | 0.132 W |
| 0.1 ohm | 0.304 W | 0.376 W |
| 0.5 ohm | 1.52 W | 1.88 W |

These values illustrate sensitivity; none establishes resistance at 3.3 V.
Higher resistance also changes the current through a passive LED strip.

## Switching sensitivity

The resistor alone limits initial gate current to at most 3.3/220 = 15 mA;
GPIO output resistance reduces it further. At an illustrative 2.5 V gate
plateau, only (3.3 - 2.5)/220 = 3.6 mA remains available for turn-on.
The actual plateau and charge need measurement at the operating current.

The common linear-overlap approximation is
`P_switch ≈ 0.5 × V × I × (rise_time + fall_time) × frequency`.
At 12 V / 2 A and a hypothetical combined 5 us transition time, it gives
0.06 W at 1 kHz, 0.30 W at 5 kHz, 0.60 W at 10 kHz, and 1.20 W at 20 kHz.
These are illustrative calculations, not measured losses or guaranteed bounds.
The LED current falls as MOSFET voltage rises, so the constant-current model
may substantially overestimate overlap loss. Transitions must also fit within
the on/off intervals for this approximation to apply.

At exactly 94% duty, off-time is only 60 us at 1 kHz, 12 us at 5 kHz,
6 us at 10 kHz, and 3 us at 20 kHz. Higher frequency therefore warrants an
analog waveform and temperature check even if the light looks steadier.
See [TI switching-loss guidance](https://www.ti.com/document-viewer/lit/html/SSZTBE2/GUID-DA3446C4-8101-4507-85A8-08E278FBE63B).

## Measurements needed

At steady 100%, measure directly at the MOSFET leads:

1. Gate-to-source DC voltage, to verify actual gate drive.
2. Drain-to-source DC voltage, plus LED current, to calculate `P = VDS × I`.

For example, 0.05 V at 2 A is 0.10 W; 0.5 V is 1 W; 1 V is 2 W.
During PWM, a handheld meter's drain-to-source reading includes off-time and
cannot replace an on-state measurement. Actual switching loss requires
simultaneous drain voltage and current waveforms integrated over a cycle;
the gate waveform helps explain slow or incomplete transitions.

Simple MOSFET series resistance generally reduces the current demanded by a
passive LED strip. It alone does not explain why the converter maintains 12 V
at 100% yet loses regulation at slightly lower duty. Converter response to
pulsed load remains the stronger lead, without proving the exact mechanism.
