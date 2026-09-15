"""Unused CW still needs UART/bridge checks, but has no acoustic target."""
import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from acoustic_tuner import motor_participates, interpolation_readback_confirmed


class MotorSelectionTest(unittest.TestCase):
    def setUp(self):
        self.cw = {
            "driverRole": "rhoCompanion", "motorConfigured": False,
            "connected": True, "uartResponseValid": True,
            "settings": {"chopconfReadValid": True, "softwareEnabled": False,
                         "chopperOffTime": 0, "interpolationTo256": True},
        }

    def test_only_explicitly_unused_cw_is_excluded(self):
        self.assertFalse(motor_participates("rhoCompanion", self.cw))
        self.assertTrue(motor_participates("rho", self.cw))
        self.assertTrue(motor_participates("rhoCompanion", {}))
        self.cw["motorConfigured"] = True
        self.assertTrue(motor_participates("rhoCompanion", self.cw))

    def test_unused_cw_requires_each_disable_check(self):
        for key in ("connected", "uartResponseValid"):
            diagnostic = copy.deepcopy(self.cw)
            diagnostic[key] = False
            with self.assertRaises(RuntimeError):
                motor_participates("rhoCompanion", diagnostic)
        for key, value in (("chopconfReadValid", False),
                           ("softwareEnabled", True), ("chopperOffTime", 3)):
            diagnostic = copy.deepcopy(self.cw)
            diagnostic["settings"][key] = value
            with self.assertRaises(RuntimeError):
                motor_participates("rhoCompanion", diagnostic)

    def test_active_interpolation_and_inactive_bridge_are_checked_separately(self):
        main = {"driverRole": "rho", "settings": {
            "chopconfReadValid": True, "interpolationTo256": False}}
        self.assertTrue(interpolation_readback_confirmed(
            [main, self.cw], {"rho", "rhoCompanion"}, False))
        self.assertFalse(interpolation_readback_confirmed(
            [main, self.cw], {"rho", "rhoCompanion"}, True))
        self.assertFalse(interpolation_readback_confirmed(
            [main], {"rho", "rhoCompanion"}, False))


if __name__ == "__main__":
    unittest.main()
