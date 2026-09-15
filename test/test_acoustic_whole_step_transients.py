"""A quiet cruise median must not hide short or endpoint STEP-gate bursts."""
import copy
import sys
import unittest
from dataclasses import asdict
from pathlib import Path
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from acoustic_tuner import (
    AXES, a_weighting_power, analyze_timed, total_sound_check,
    whole_step_transient_metrics,
)


def intervals(*bounds):
    return [dict(epoch=i + 1, startHostS=start, stopHostS=stop,
                 startUncertaintyS=.01, stopUncertaintyS=.01)
            for i, (start, stop) in enumerate(bounds)]


class WholeStepTransientTest(unittest.TestCase):
    def measure(self, times, powers, gates):
        return whole_step_transient_metrics(times, powers, gates, .042667, .01)

    def test_sparse_burst_cannot_be_diluted_by_a_long_gate(self):
        times = np.arange(.05, 67, .02)
        powers = np.full(len(times), 1e-7)
        powers[(times > 32) & (times < 32.2)] = 1e-4
        self.assertEqual(np.percentile(powers[(times > 3) & (times < 63)], 95), 1e-7)
        result = self.measure(times, powers, intervals((3, 63)))
        self.assertTrue(result["valid"])
        self.assertAlmostEqual(result["gates"][0]["max100msAWeightedDbfs"], -40)
        self.assertGreater(result["gates"][0]["max100msGainOverAdjacentIdleMaxDb"], 29)

    def test_each_repeated_direction_and_uncertain_edge_is_covered(self):
        times = np.arange(.05, 25, .02)
        gates = intervals((3, 5), (8, 10), (13, 15), (18, 20))
        for index, gate in enumerate(gates):
            powers = np.full(len(times), 1e-7)
            # Include the end boundary itself, not only steady cruise.
            powers[np.abs(times - gate["stopHostS"]) < .06] = 1e-4
            result = self.measure(times, powers, gates)
            self.assertTrue(result["valid"])
            self.assertEqual(len(result["gates"]), 4)
            self.assertGreater(result["gates"][index]["max100msAWeightedDbfs"], -42)
            self.assertTrue(all(g["max100msAWeightedDbfs"] == -70
                                for i, g in enumerate(result["gates"]) if i != index))

    def test_neighboring_idle_is_diagnostic_and_never_subtracted(self):
        times = np.arange(.05, 14, .02)
        powers = np.full(len(times), 1e-5)
        result = self.measure(times, powers, intervals((3, 5), (8, 10)))
        self.assertTrue(result["valid"])
        for gate in result["gates"]:
            self.assertEqual(gate["max100msAWeightedDbfs"], -50)
            self.assertEqual(gate["preIdle100msMaxAWeightedDbfs"], -50)
            self.assertEqual(gate["max100msGainOverAdjacentIdleMaxDb"], 0)

    def test_invalid_clipped_or_missing_exact_data_fail_closed(self):
        times = np.arange(.05, 14, .02)
        power = np.full(len(times), 1e-7)
        self.assertFalse(self.measure(times, power, [])["valid"])
        self.assertFalse(self.measure(times, power, intervals((.08, 5)))["valid"])
        self.assertFalse(self.measure(times, power, intervals((3, 13.95)))["valid"])
        self.assertFalse(self.measure(times, power, intervals((8, 10), (3, 5)))["valid"])
        bad = times.copy(); bad[30] += .001
        self.assertFalse(self.measure(bad, power, intervals((3, 5)))["valid"])
        for bad_value in (float("nan"), float("inf"), -1):
            bad_power = power.copy(); bad_power[30] = bad_value
            self.assertFalse(self.measure(times, bad_power, intervals((3, 5)))["valid"])

    def test_analyze_endpoint_burst_survives_old_coarse_envelope_clipping(self):
        times = np.arange(.05, 14, .02)
        gates = intervals((3, 5), (8, 10))
        power = np.full(len(times), 1e-7)
        power[(times > 9.72) & (times < 9.9)] = 1e-4
        frequencies = np.arange(0, 4001, 100, dtype=float)
        spectra = np.zeros((len(times), len(frequencies)))
        spectra[:, 10] = power / a_weighting_power(frequencies)[10]
        telemetry = []
        for t in times:
            velocity = 2 if 3 <= t <= 5 else -2 if 8 <= t <= 10 else 0
            telemetry.append(dict(hostOffsetS=float(t),
                                  state="RUNNING" if velocity else "IDLE",
                                  velocity=dict(rho=velocity, theta=0)))
        with patch("acoustic_tuner.read_wav", return_value=(np.zeros(2400), 48000)), \
                patch("acoustic_tuner.short_window_spectrogram", return_value=(times, frequencies, spectra)), \
                patch("acoustic_tuner.step_motion_intervals", return_value=gates):
            metrics, _ = analyze_timed(Path("unused.wav"), 3, 9.7, telemetry=telemetry,
                                       axis=AXES["rho"], expected_max_velocity=2)
        self.assertEqual(metrics.raw_gate_p95_upper_95_a_weighted_dbfs, [-70, -70])
        self.assertEqual(metrics.whole_step_transient["gates"][1]["max100msAWeightedDbfs"], -40)
        repeat = dict(metrics=asdict(metrics), timingQuality=dict(valid=True), gainFingerprintStable=True)
        self.assertTrue(total_sound_check([repeat], "verify", 2, -60)["withinTier"])
        checked = total_sound_check([repeat], "verify", 2, -60, require_whole_step=True)
        self.assertTrue(checked["measurementValid"])
        self.assertFalse(checked["withinTier"])
        self.assertEqual(checked["status"], "inconclusive-total-above-tier")


class WholeStepAcceptanceTest(unittest.TestCase):
    def setUp(self):
        times = np.arange(.05, 14, .02)
        whole = whole_step_transient_metrics(times, np.full(len(times), 1e-7),
                                             intervals((3, 5), (8, 10)), .042667, .01)
        self.repeat = dict(timingQuality=dict(valid=True), gainFingerprintStable=True,
            metrics=dict(background_stable=True, clipping_detected=False,
                all_local_background_stable=True, on_off_pair_count=2,
                all_gates_sustain_commanded_velocity=True,
                raw_pre_idle_median_upper_95_a_weighted_dbfs=-70,
                raw_post_idle_median_upper_95_a_weighted_dbfs=-70,
                raw_gate_median_upper_95_a_weighted_dbfs=[-70, -70],
                raw_gate_pre_idle_median_upper_95_a_weighted_dbfs=[-70, -70],
                raw_gate_post_idle_median_upper_95_a_weighted_dbfs=[-70, -70],
                whole_step_transient=whole))

    def test_quiet_passes_but_missing_or_partial_step_gates_do_not(self):
        check = lambda r: total_sound_check([r], "verify", 2, -60, require_whole_step=True)
        self.assertTrue(check(self.repeat)["withinTier"])
        for bad in ({}, dict(valid=False, gates=[]),
                    dict(valid=True, gates=self.repeat["metrics"]["whole_step_transient"]["gates"][:1])):
            changed = copy.deepcopy(self.repeat)
            changed["metrics"]["whole_step_transient"] = bad
            self.assertFalse(check(changed)["measurementValid"])
            # Legacy/theta acceptance is deliberately unchanged.
            self.assertTrue(total_sound_check([changed], "verify", 2, -60)["withinTier"])

    def test_raw_room_or_motor_exceedance_remains_inconclusive(self):
        self.repeat["metrics"]["whole_step_transient"]["gates"][1]["max100msAWeightedDbfs"] = -50
        result = total_sound_check([self.repeat], "verify", 2, -60, require_whole_step=True)
        self.assertEqual(result["status"], "inconclusive-total-above-tier")
        self.assertEqual(result["loudestWholeStep100msAWeightedDbfs"], -50)
        self.assertIn("does not identify", result["limitation"])


if __name__ == "__main__":
    unittest.main()
