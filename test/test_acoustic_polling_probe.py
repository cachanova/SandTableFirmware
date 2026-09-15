import copy
import sys
import unittest
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from acoustic_polling_probe import require_unchanged_idle, summarize_power


class StationaryProbeTest(unittest.TestCase):
    def setUp(self):
        self.initial = dict(state="IDLE", position={"rho": 0, "theta": 0},
                            velocity={"rho": 0, "theta": 0},
                            planner={"timerActive": False, "running": False, "underruns": 0},
                            stepMotion={"active": False, "epoch": 102})

    def test_unchanged_idle_accepted(self):
        require_unchanged_idle(self.initial, self.initial)

    def test_raw_power_converted_to_dbfs_including_upper_bound(self):
        summary = summarize_power(np.full(100, 1e-6))
        for key in ("rawMedianAWeightedDbfs", "rawP95AWeightedDbfs", "rawP95Upper95AWeightedDbfs"):
            self.assertAlmostEqual(summary[key], -60)
        self.assertEqual(summary["fractionAboveMinus55"], 0)

    def test_motion_or_lost_origin_rejected(self):
        for group, key, value in (("position", "rho", .01), ("velocity", "theta", .1),
                                   ("stepMotion", "epoch", 103), ("stepMotion", "epoch", 0),
                                   ("stepMotion", "active", True),
                                   ("planner", "timerActive", True),
                                   ("planner", "running", True), ("planner", "underruns", 1),
                                   ("position", "rho", float("nan"))):
            sample = copy.deepcopy(self.initial)
            sample[group][key] = value
            with self.assertRaises(RuntimeError):
                require_unchanged_idle(sample, self.initial)

    def test_incomplete_telemetry_rejected(self):
        with self.assertRaises(KeyError):
            require_unchanged_idle({}, self.initial)


if __name__ == "__main__":
    unittest.main()
