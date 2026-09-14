"""Host-side checks for lossless homing trace collection."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from rho_homing_tuner import (  # noqa: E402
    HomingTraceCollector,
    configured_motor_axes,
    disabled_companion_verified,
    require_expected_motors,
    validate_trial,
)


def snapshot(cycle: int, total: int, first: int) -> dict:
    samples = [{"t": index} for index in range(first, total)]
    return {"cycle": cycle, "total": total, "count": len(samples),
            "samples": samples}


class HomingTraceCollectorTest(unittest.TestCase):
    def test_joins_overlapping_ring_windows(self) -> None:
        collector = HomingTraceCollector()
        collector.add(snapshot(3, 0, 0))
        collector.add(snapshot(3, 5, 0))
        collector.add(snapshot(3, 9, 3))
        self.assertEqual([sample["t"] for sample in collector.result()["samples"]],
                         list(range(9)))

    def test_rejects_lost_samples(self) -> None:
        collector = HomingTraceCollector()
        collector.add(snapshot(3, 2, 0))
        with self.assertRaisesRegex(RuntimeError, "lost samples"):
            collector.add(snapshot(3, 9, 4))

    def test_rejects_cycle_change(self) -> None:
        collector = HomingTraceCollector()
        collector.add(snapshot(3, 2, 0))
        with self.assertRaisesRegex(RuntimeError, "cycle changed"):
            collector.add(snapshot(4, 1, 0))

    def test_rejects_old_firmware_without_total(self) -> None:
        collector = HomingTraceCollector()
        with self.assertRaisesRegex(RuntimeError, "lacks a total"):
            collector.add({"cycle": 3, "count": 0, "samples": []})

    def test_rejects_cumulative_contact_overrun(self) -> None:
        report = {
            "valid": True, "uartValidFraction": 1.0,
            "terminalToBaselineRatio": 0.2,
            "terminalSteps": 3440, "expectedContactSteps": 3200,
            "overrunMm": 0.6,
        }
        reports = {
            "rho-coarse": report,
            "rho-precision": {**report, "terminalSteps": 1840,
                              "expectedContactSteps": 1600,
                              "overrunMm": 0.6},
        }
        self.assertTrue(any("shared 1 mm" in failure for failure in
                            validate_trial(reports, 400.0)))

    def test_rejects_two_early_triggers(self) -> None:
        report = {
            "valid": True, "uartValidFraction": 1.0,
            "terminalToBaselineRatio": 0.2,
            "terminalSteps": 2800, "expectedContactSteps": 3200,
            "overrunMm": 0.0,
        }
        reports = {
            "rho-coarse": report,
            "rho-precision": {**report, "terminalSteps": 1200,
                              "expectedContactSteps": 1600},
        }
        self.assertTrue(any("combined return" in failure for failure in
                            validate_trial(reports, 400.0)))

    def test_main_only_report_does_not_require_cw_samples(self) -> None:
        report = {
            "valid": True, "uartValidFraction": 1.0,
            "terminalToBaselineRatio": 0.2,
            "terminalSteps": 3200, "expectedContactSteps": 3200,
            "overrunMm": 0.0,
        }
        reports = {
            "rho-coarse": report,
            "rho-precision": {**report, "terminalSteps": 1600,
                              "expectedContactSteps": 1600},
        }
        self.assertEqual(validate_trial(reports, 400.0), [])

    def test_main_only_requires_cw_bridge_off(self) -> None:
        self.assertEqual(configured_motor_axes({"companionMotorEnabled": False}),
                         (1,))
        self.assertEqual(configured_motor_axes({"companionMotorEnabled": True}),
                         (1, 2))
        with self.assertRaisesRegex(RuntimeError, "configured RHO motors"):
            configured_motor_axes({})
        driver = {
            "connected": True, "uartResponseValid": True,
            "motorConfigured": False,
            "settings": {"softwareEnabled": False, "chopperOffTime": 0},
        }
        self.assertTrue(disabled_companion_verified(driver))
        self.assertFalse(disabled_companion_verified({
            **driver, "settings": {**driver["settings"], "chopperOffTime": 3}
        }))
        require_expected_motors((1,), "main")
        with self.assertRaisesRegex(RuntimeError, "do not match"):
            require_expected_motors((1, 2), "main")


if __name__ == "__main__":
    unittest.main()
