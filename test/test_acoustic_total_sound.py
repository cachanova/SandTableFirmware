"""Total sound must retain energized hold noise and spatially local noise."""
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from acoustic_tuner import AXES, a_weighting_power, analyze_timed, raw_percentile_upper_95


class RawPowerBoundTest(unittest.TestCase):
    def test_constant_power_and_deterministic_percentiles(self):
        self.assertEqual(raw_percentile_upper_95(np.full(40, 1e-6), 50, 1), 1e-6)
        values = np.geomspace(1e-9, 1e-5, 200)
        bound = raw_percentile_upper_95(values, 95, 12)
        self.assertEqual(bound, raw_percentile_upper_95(values, 95, 12))
        self.assertGreaterEqual(bound, np.percentile(values[::4], 95))
        self.assertEqual(raw_percentile_upper_95(np.zeros(40), 50, 1), 0)

    def test_insufficient_or_invalid_values_fail_closed(self):
        for values in ([], np.ones(16), [1, float("nan")] * 20,
                       [1, float("inf")] * 20, [1, -1] * 20,
                       np.ones((20, 2))):
            self.assertIsNone(raw_percentile_upper_95(values, 50, 1))
        for percentile in (-1, 101, float("nan")):
            with self.assertRaises(ValueError):
                raw_percentile_upper_95(np.ones(40), percentile, 1)


class TotalSoundMetricTest(unittest.TestCase):
    def analyze(self, idle_db=-80, motion_db=-70, loud_gate=None,
                loud_idle=False, ramp_spike=False):
        times = np.arange(0, 24, .02)
        intervals = ((3, 5), (8, 10), (13, 15), (18, 20))
        powers = np.full(len(times), 10 ** (idle_db / 10))
        velocities = np.zeros(len(times))
        for index, (start, stop) in enumerate(intervals):
            mask = (times >= start) & (times < stop)
            powers[mask] = 10 ** (motion_db / 10)
            velocities[mask] = .1 if index % 2 == 0 else -.1
            if index == loud_gate:
                powers[mask] = 1e-5
            if ramp_spike:
                ramp = mask & (times < start + .6)
                velocities[ramp] *= .5
                powers[ramp] = 1e-5
        if loud_idle:
            powers[(times >= 10) & (times < 13)] = 1e-5
        frequencies = np.arange(0, 4001, 100, dtype=float)
        spectra = np.zeros((len(times), len(frequencies)))
        spectra[:, 10] = powers / a_weighting_power(frequencies)[10]
        telemetry = [dict(hostOffsetS=float(t), state="RUNNING" if v else "IDLE",
                          velocity=dict(theta=float(v), rho=0.0))
                     for t, v in zip(times, velocities)]
        # Audio rate controls transition uncertainty and audible bandwidth.
        # Use 48 kHz metadata without allocating a long PCM fixture.
        with patch("acoustic_tuner.read_wav", return_value=(np.zeros(2400), 48000)), \
                patch("acoustic_tuner.short_window_spectrogram",
                      return_value=(times, frequencies, spectra)):
            return analyze_timed(
                Path("unused.wav"), 3, 20, telemetry=telemetry,
                axis=AXES["theta"], expected_max_velocity=.1,
            )[0]

    def test_persistent_hold_noise_is_not_subtracted_from_total(self):
        metrics = self.analyze(idle_db=-55, motion_db=-54.8)
        self.assertLess(metrics.loudest_broadband_gate_excess_upper_95_a_weighted_dbfs, -60)
        self.assertEqual(metrics.raw_gate_median_upper_95_a_weighted_dbfs, [-54.8] * 4)
        self.assertEqual(metrics.raw_gate_pre_idle_median_upper_95_a_weighted_dbfs, [-55] * 4)
        self.assertEqual(metrics.raw_gate_post_idle_median_upper_95_a_weighted_dbfs, [-55] * 4)

    def test_quiet_total_retains_all_gate_bounds(self):
        metrics = self.analyze()
        self.assertEqual(metrics.on_off_pair_count, 4)
        self.assertEqual(metrics.raw_gate_median_upper_95_a_weighted_dbfs, [-70] * 4)
        self.assertEqual(metrics.raw_gate_p95_upper_95_a_weighted_dbfs, [-70] * 4)
        self.assertEqual(metrics.raw_motion_p95_upper_95_a_weighted_dbfs, -70)
        self.assertEqual(metrics.raw_pre_idle_median_upper_95_a_weighted_dbfs, -80)
        self.assertEqual(metrics.raw_post_idle_median_upper_95_a_weighted_dbfs, -80)

    def test_one_loud_spatial_gate_cannot_be_pooled_away(self):
        metrics = self.analyze(loud_gate=2)
        self.assertEqual(metrics.raw_gate_median_upper_95_a_weighted_dbfs, [-70, -70, -50, -70])
        self.assertEqual(metrics.raw_gate_p95_upper_95_a_weighted_dbfs[2], -50)

    def test_local_idle_noise_survives_quiet_pooled_background(self):
        metrics = self.analyze(loud_idle=True)
        self.assertEqual(metrics.raw_pre_idle_median_upper_95_a_weighted_dbfs, -80)
        self.assertEqual(metrics.raw_post_idle_median_upper_95_a_weighted_dbfs, -80)
        self.assertEqual(metrics.raw_gate_post_idle_median_upper_95_a_weighted_dbfs[1], -50)
        self.assertEqual(metrics.raw_gate_pre_idle_median_upper_95_a_weighted_dbfs[2], -50)

    def test_raw_p95_keeps_ramps_while_cruise_median_stays_quiet(self):
        metrics = self.analyze(ramp_spike=True)
        self.assertEqual(metrics.raw_gate_median_upper_95_a_weighted_dbfs, [-70] * 4)
        self.assertEqual(metrics.raw_gate_p95_upper_95_a_weighted_dbfs, [-50] * 4)
        self.assertEqual(metrics.raw_motion_p95_upper_95_a_weighted_dbfs, -50)


if __name__ == "__main__":
    unittest.main()
