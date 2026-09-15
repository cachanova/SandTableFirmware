"""Total sound must not hide steady driver noise inside the idle reference."""
import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from acoustic_tuner import total_sound_check


class TotalAcceptanceTest(unittest.TestCase):
    def setUp(self):
        self.repeat = dict(timingQuality={"valid": True}, gainFingerprintStable=True,
            metrics=dict(background_stable=True, clipping_detected=False,
                all_local_background_stable=True, on_off_pair_count=2,
                all_gates_sustain_commanded_velocity=True,
                raw_pre_idle_median_upper_95_a_weighted_dbfs=-69,
                raw_post_idle_median_upper_95_a_weighted_dbfs=-69,
                raw_gate_median_upper_95_a_weighted_dbfs=[-65, -64],
                raw_gate_p95_upper_95_a_weighted_dbfs=[-65, -58],
                raw_gate_pre_idle_median_upper_95_a_weighted_dbfs=[-69, -69],
                raw_gate_post_idle_median_upper_95_a_weighted_dbfs=[-69, -69],
                raw_motion_p95_upper_95_a_weighted_dbfs=-58))

    def check(self, profile="gated", count=2):
        return total_sound_check([self.repeat], profile, count, -60)

    def test_quiet_motion_and_idle_pass(self):
        self.assertTrue(self.check()["withinTier"])

    def test_loud_hold_or_room_is_inconclusive_not_quiet(self):
        self.repeat["metrics"]["raw_gate_pre_idle_median_upper_95_a_weighted_dbfs"][1] = -55
        self.assertEqual(self.check()["status"], "inconclusive-total-above-tier")
        self.assertFalse(self.check()["withinTier"])

    def test_one_loud_spatial_gate_is_not_averaged_away(self):
        self.repeat["metrics"]["raw_gate_median_upper_95_a_weighted_dbfs"][1] = -59
        self.assertFalse(self.check()["withinTier"])

    def test_missing_short_or_invalid_measurements_fail_closed(self):
        original = copy.deepcopy(self.repeat)
        for value in ([], [-69], [-69, None], [-69, float("nan")]):
            self.repeat["metrics"]["raw_gate_post_idle_median_upper_95_a_weighted_dbfs"] = value
            self.assertFalse(self.check()["measurementValid"])
        self.repeat = original
        self.repeat["timingQuality"]["valid"] = False
        self.assertFalse(self.check()["measurementValid"])
        self.repeat["timingQuality"]["valid"] = True
        self.repeat["gainFingerprintStable"] = False
        self.assertFalse(self.check()["measurementValid"])

    def test_ramp_and_stress_keep_loud_transients_visible(self):
        self.assertFalse(self.check("ramp")["withinTier"])
        self.assertFalse(self.check("stress", 0)["withinTier"])

    def test_actual_stress_pair_count_uses_p95_without_cruise_requirement(self):
        self.repeat["metrics"]["all_gates_sustain_commanded_velocity"] = None
        self.repeat["metrics"]["raw_motion_p95_upper_95_a_weighted_dbfs"] = -65
        self.assertTrue(self.check("stress", 1)["withinTier"])
        self.repeat["metrics"]["raw_motion_p95_upper_95_a_weighted_dbfs"] = -58
        self.assertEqual(self.check("stress", 1)["status"], "inconclusive-total-above-tier")

    def test_empty_input_cannot_pass(self):
        self.assertFalse(total_sound_check([], "gated", 2, -60)["withinTier"])


if __name__ == "__main__":
    unittest.main()
