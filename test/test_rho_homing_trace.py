"""Host-side checks for lossless homing trace collection."""

import json
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
from rho_homing_replay import replay_artifact, replay_phase  # noqa: E402
from rho_homing_consensus import contact_scatter  # noqa: E402


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
            "rho-precision": {**report, "terminalSteps": 1800,
                              "expectedContactSteps": 1600,
                              "overrunMm": 0.5},
        }
        self.assertTrue(any("shared 1 mm" in failure for failure in
                            validate_trial(reports, 400.0)))
        self.assertEqual(validate_trial(reports, 400.0, 2.0), [])

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

    def test_three_contacts_tolerate_one_prior_outlier(self) -> None:
        def reports_for(errors):
            return {f"rho-{'coarse' if index == 0 else 'precision-' + str(index+1)}": {
                "valid": True, "uartValidFraction": 1.0,
                "terminalToBaselineRatio": 0.5,
                "terminalSteps": 3200 + round(error * 400),
                "expectedContactSteps": 3200,
                "overrunMm": max(0, error),
            } for index, error in enumerate(errors)}
        self.assertEqual(validate_trial(
            reports_for([-0.6, 0.6, 0.02, -0.01]), 400, 2, 0.4, 5, 3), [])
        self.assertTrue(any("span" in failure for failure in validate_trial(
            reports_for([0, 0.3, 0.3]), 400, 2, 0.4, 5, 3)))
        self.assertTrue(any("shared" in failure for failure in validate_trial(
            reports_for([1.5, 1.5, 1.5, 1.5]), 400, 2, 0.4, 5, 3)))

    def test_phase_report_separates_repeated_precision_passes(self) -> None:
        samples = [
            {"a": 1, "p": 4, "n": 2, "t": 1000, "s": 3200, "g": 100, "v": True},
            {"a": 1, "p": 4, "n": 3, "t": 2000, "s": 3300, "g": 80, "v": True},
        ]
        report = phase_report(samples, 1, 4, 3200, np.array([]), np.array([]), 0, 400, 2)
        self.assertEqual(report["terminalSteps"], 3200)
        self.assertEqual(report["sampleCount"], 1)

    def test_missing_contacts_cannot_pass_validation(self) -> None:
        self.assertTrue(validate_trial({}, 400, 2, 0.4, 5, 3))
        report = {
            "valid": True, "uartValidFraction": 1.0,
            "terminalToBaselineRatio": 0.5,
            "terminalSteps": 3200, "expectedContactSteps": 3200,
            "overrunMm": 0.0, "contactDetected": False,
        }
        reports = {f"rho-{suffix}": report for suffix in
                   ("coarse", "precision", "precision-3")}
        self.assertTrue(any("no detected contacts" in error for error in
                            validate_trial(reports, 400, 2, 0.4, 5, 3)))
        companion = {name.replace("rho-", "rho-companion-", 1): {
            **report, "contactDetected": True} for name in reports}
        self.assertTrue(any("rho: missing" in error for error in
                            validate_trial(companion, 400, 2, 0.4, 5, 3, (1, 2))))

    def test_rolling_search_discards_early_candidate_without_spending_overrun(self) -> None:
        reports = {}
        home = furthest = 7200
        for n, coordinate in enumerate([2868, 7240, 7260, 7250], 1):
            suffix = "coarse" if n == 1 else "precision" if n == 2 else f"precision-{n}"
            reports[f"rho-{suffix}"] = {
                "valid": True, "contactDetected": True, "uartValidFraction": 1,
                "terminalToBaselineRatio": 0.5,
                "terminalSteps": coordinate, "expectedContactSteps": furthest,
                "ledgerCoordinateSteps": coordinate, "knownHomeCoordinateSteps": home,
                "overrunMm": max(0, coordinate - furthest) / 400,
            }
            furthest = max(furthest, coordinate)
        self.assertEqual(validate_trial(reports, 400, 2, .4, 5, 3, (1,), True), [])
        for report in reports.values():
            report["ledgerCoordinateSteps"] -= 2000
        self.assertTrue(any("short of known home" in error for error in
                            validate_trial(reports, 400, 2, .4, 5, 3, (1,), True)))

    def test_phase_report_uses_actual_detector_baseline(self) -> None:
        samples = [{"a": 1, "p": 4, "n": 2, "t": index * 10,
                    "s": index * 8, "g": 258, "v": True}
                   for index in range(10)]
        samples.append({"a": 1, "p": 4, "n": 2, "t": 100,
                        "s": 3200, "g": 224, "v": True, "b": 270, "h": 229})
        report = phase_report(samples, 1, 4, 3200, np.array([]), np.array([]), 0, 400, 2)
        self.assertEqual(report["baselineMedianSg"], 258)
        self.assertLess(report["terminalToBaselineRatio"], 0.85)

    def test_replay_does_not_join_distinct_precision_passes(self) -> None:
        samples = []
        for pass_number, values in ((2, [200] * 60 + [40] * 9),
                                    (3, [200] * 69)):
            samples.extend({"a": 1, "p": 4, "n": pass_number,
                            "t": pass_number * 1000 + index * 10,
                            "s": index * 8, "g": value, "v": True}
                           for index, value in enumerate(values))
        report = replay_artifact({"settings": {
            "homingStepsPerMm": 400, "coarseVelocityMmS": 2,
            "minimumTravelMs": 0, "homingMicrosteps": 8,
        }, "trace": samples}, 0.85, 5)
        self.assertIsNotNone(report["precision"])
        self.assertIsNone(report["precision-3"])

    def test_scatter_requires_marked_consecutive_contacts(self) -> None:
        artifact = {"settings": {
            "traceContactMarkers": True, "homingStepsPerMm": 400,
            "knownRhoStartMm": 0, "runwayMm": 8, "backoffMm": 8,
        }, "trace": [], "terminalStatus": {"state": "HOMING_FAILED"},
            "instrumentedPass": False}
        self.assertFalse(contact_scatter(artifact)["usable"])
        artifact["trace"] = [{"a": 1, "n": n, "b": 200, "s": s}
                             for n, s in ((1, 3220), (2, 3240), (3, 3180))]
        self.assertEqual(contact_scatter(artifact)["threeContactSpansMm"], [0.1])
        artifact["trace"][1]["n"] = 3
        self.assertFalse(contact_scatter(artifact)["usable"])

    def test_recorded_near_zero_pass_does_not_qualify_false_contact(self) -> None:
        directory = (Path(__file__).resolve().parents[1] /
                     "tuning-recordings/rho-main-only-20260915")
        good = json.loads((directory /
            "20260915T024400Z-rho-home-p85-n5-start-r0-cw0mm-result.json").read_text())
        bad = json.loads((directory /
            "20260915T025442Z-rho-home-p85-n5-start-r10-cw0mm-result.json").read_text())
        self.assertEqual(validate_trial(good["reports"], 400, 2, 0.4, 5, 3), [])
        self.assertTrue(validate_trial(bad["reports"], 400, 2, 0.4, 5, 3))
        self.assertEqual(contact_scatter(good)["threeContactSpansMm"], [0.1625])
        self.assertEqual(contact_scatter(bad)["contactCoordinatesMm"], [-10.83])

    def test_recorded_rolling_search_recovers_from_two_early_contacts(self) -> None:
        path = (Path(__file__).resolve().parents[1] /
                "tuning-recordings/rho-main-only-20260915/"
                "20260915T032402Z-rho-home-p85-n5-start-r7.2075-cw0mm-result.json")
        artifact = json.loads(path.read_text())
        self.assertEqual(validate_trial(artifact["reports"], 400, 2, .4, 5, 3, (1,), True), [])
        self.assertEqual(contact_scatter(artifact)["contactCoordinatesMm"],
                         [-4.8275, -1.31, -.17, -.285, .0025])

    def test_recorded_tight_false_consensus_is_not_home(self) -> None:
        directory = (Path(__file__).resolve().parents[1] /
                     "tuning-recordings/rho-main-only-20260915")
        bad = json.loads((directory /
            "20260915T033455Z-rho-home-p85-n5-start-r25-cw0mm-result.json").read_text())
        good = json.loads((directory /
            "20260915T034351Z-rho-home-p85-n5-start-r21.7875-cw0mm-result.json").read_text())
        self.assertEqual(contact_scatter(bad)["threeContactSpansMm"], [0.36])
        self.assertTrue(any("short of known home" in error for error in
                            validate_trial(bad["reports"], 400, 2, .4, 5, 3, (1,), True)))
        self.assertEqual(good["settings"]["pulseSource"], "hardware-timer")
        self.assertEqual(validate_trial(good["reports"], 400, 2, .4, 5, 3, (1,), True), [])
        self.assertEqual(contact_scatter(good)["threeContactSpansMm"][-1], .08)

    def test_recorded_long_approaches_preserve_the_known_position_ledger(self) -> None:
        directory = (Path(__file__).resolve().parents[1] /
                     "tuning-recordings/rho-main-only-20260915")
        for filename in (
            "20260915T035505Z-rho-home-p85-n5-start100mm-result.json",
            "20260915T041353Z-rho-home-p85-n5-start200mm-result.json",
            "20260915T042135Z-rho-home-p85-n5-start400mm-result.json",
        ):
            with self.subTest(filename=filename):
                artifact = json.loads((directory / filename).read_text())
                self.assertEqual(artifact["settings"]["pulseSource"], "hardware-timer")
                self.assertEqual(artifact["settings"]["backoffMm"], 6)
                self.assertEqual(validate_trial(
                    artifact["reports"], 400, 2, .4, 5, 3, (1,), True), [])
                scatter = contact_scatter(artifact)
                self.assertEqual(scatter["passes"], [1, 2, 3])
                self.assertLessEqual(max(abs(value) for value in
                    scatter["contactCoordinatesMm"]), .4)
                # These checks concern commands and SG, not camera acceptance.
                self.assertEqual(scatter["physicalContactLabels"],
                                 "requires synchronized external observation")

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

    def test_detector_replay_full_step_alternating_stop(self) -> None:
        values = [180] * 60 + [150, 160, 146, 160, 0, 160, 0]
        samples = [
            {"t": index * 7, "s": index, "g": value, "v": True}
            for index, value in enumerate(values)
        ]
        result = replay_phase(
            samples, ratio=0.75, votes=5, minimum_steps=0,
            steps_per_second=300, external_steps_per_full_step=1,
        )
        self.assertEqual(result["sg"], 0)
        self.assertEqual(result["steps"], len(samples) - 1)

        isolated = [180] * 60 + [150, 128, 146, 120, 0, 128, 150, 154, 148]
        isolated_samples = [
            {"t": index * 7, "s": index, "g": value, "v": True}
            for index, value in enumerate(isolated)
        ]
        self.assertIsNone(replay_phase(
            isolated_samples, ratio=0.75, votes=5, minimum_steps=0,
            steps_per_second=300, external_steps_per_full_step=1,
        ))

    def test_runway_arming_rejects_recorded_mid_travel_false_trigger(self) -> None:
        path = (Path(__file__).resolve().parents[1] /
                "tuning-recordings/rho-main-only-20260914/"
                "20260914T175722Z-rho-home-p75-n5-start-r0-cw0mm-result.json")
        artifact = json.loads(path.read_text(encoding="utf-8"))
        coarse = [sample for sample in artifact["trace"]
                  if sample["a"] == 1 and sample["p"] == 2]
        old = replay_phase(
            coarse, ratio=0.75, votes=5, minimum_steps=150,
            steps_per_second=300, external_steps_per_full_step=1,
        )
        guarded = replay_phase(
            coarse, ratio=0.75, votes=5, minimum_steps=350,
            steps_per_second=300, external_steps_per_full_step=1,
        )
        self.assertEqual(old["steps"], 217)
        self.assertIsNone(guarded)

    def test_replay_preserves_precision_trigger_at_time_gate(self) -> None:
        path = (Path(__file__).resolve().parents[1] /
                "tuning-recordings/rho-main-only-20260914/"
                "20260914T181817Z-rho-home-p75-n5-start-r0-cw0mm-result.json")
        artifact = json.loads(path.read_text(encoding="utf-8"))
        report = replay_artifact(artifact, 0.75, 5)
        self.assertEqual(report["coarse"]["steps"], 400)
        self.assertEqual(report["precision"]["steps"], 196)


if __name__ == "__main__":
    unittest.main()
