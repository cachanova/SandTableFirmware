"""Host-side checks for lossless homing trace collection."""

import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from rho_homing_tuner import (  # noqa: E402
    HomingTraceCollector,
    configured_motor_axes,
    disabled_companion_verified,
    phase_report,
    require_expected_motors,
    validate_trial,
)
from rho_homing_replay import replay_phase  # noqa: E402


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

    def test_phase_report_uses_only_its_trace_and_correlated_audio(self) -> None:
        samples = [
            {"a": 1, "p": 2, "t": 1000, "s": 3000, "g": 200, "v": True},
            {"a": 1, "p": 2, "t": 1100, "s": 3200, "g": 100, "v": True},
            {"a": 1, "p": 4, "t": 1300, "s": 1600, "g": 50, "v": True},
        ]
        report = phase_report(
            samples, 1, 2, 3200,
            np.array([0.5, 0.8, 1.0, 1.1, 1.2, 1.3]),
            np.array([-60.0, -60.0, -60.0, -45.0, -60.0, -50.0]),
            0.0, 400.0,
        )
        self.assertEqual(report["sampleCount"], 2)
        self.assertEqual(report["terminalSteps"], 3200)
        self.assertEqual(report["audioRiseDb"], 15.0)
        self.assertTrue(report["audioCorroborated"])

    def test_detector_replay_rejects_phase_ripple_then_accepts_collapse(self) -> None:
        values = [200] * 48 + [125, 200] * 4 + [125]
        samples = [{"t": index * 10, "s": index * 8, "g": value, "v": True}
                   for index, value in enumerate(values)]
        self.assertIsNone(replay_phase(
            samples, ratio=0.75, votes=5, minimum_steps=0,
            steps_per_second=800,
        ))
        samples.extend(
            {"t": index * 10, "s": index * 8, "g": 50, "v": True}
            for index in range(len(samples), len(samples) + 9)
        )
        self.assertIsNotNone(replay_phase(
            samples, ratio=0.75, votes=5, minimum_steps=0,
            steps_per_second=800,
        ))

    def test_detector_replay_arms_by_steps_not_only_elapsed_time(self) -> None:
        samples = [
            {"t": index * 100, "s": index * 8,
             "g": 200 if index < 48 else 0, "v": True}
            for index in range(57)
        ]
        self.assertIsNone(replay_phase(
            samples, ratio=0.75, votes=5, minimum_steps=500,
            steps_per_second=100000,
        ))


if __name__ == "__main__":
    unittest.main()
