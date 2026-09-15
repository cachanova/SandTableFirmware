"""Unused CW still needs UART/bridge checks, but has no acoustic target."""
import copy
import importlib.util
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch, Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from acoustic_tuner import (motor_participates, interpolation_readback_confirmed,
                            CruiseDriverGuard, planner_repeat_healthy,
                            rho_segment_targets, rho_profile_distance_mm,
                            repeat_confirmation_satisfied, save_timing_plot, AXES,
                            precondition_rho_stealthchop, motion_driver_poll_phase)


class CalibrationCleanupTest(unittest.TestCase):
    def test_calibration_stops_on_out_of_envelope_or_planner_fault(self):
        settings = dict(runCurrent=275, highSensitivityCurrentScale=True,
                        stealthChopEnabled=True, automaticCurrentScaling=True,
                        automaticGradientAdaptation=True)
        initial = dict(state="IDLE", position={"rho": 0}, velocity={"rho": 0},
                       planner={"underruns": 0, "maxConsecutiveUnderruns": 0})
        for position, underruns, message in ((-0.1, 0, "bounded"),
                                             (100.1, 0, "bounded"),
                                             (1, 1, "underrun")):
            sample = copy.deepcopy(initial)
            sample.update(state="RUNNING", position={"rho": position},
                          velocity={"rho": 4})
            sample["planner"]["underruns"] = underruns
            board = Mock()
            board.get.side_effect = [initial, sample]
            with self.assertRaisesRegex(RuntimeError, message):
                precondition_rho_stealthchop(board, 4, settings, .11)
            board.recovering_stop.assert_called_once_with()
            board.post.assert_called_once_with("/api/tuning/test/rho/segment", {"targetMm": 100})

    def test_fault_transport_and_interrupt_always_stop_without_return_motion(self):
        for failure in (RuntimeError("driver"), OSError("transport"), KeyboardInterrupt()):
            board = Mock()
            with patch("acoustic_tuner._precondition_rho_stealthchop", side_effect=failure):
                with self.assertRaises(type(failure)):
                    precondition_rho_stealthchop(board, 4, {}, .11)
            board.recovering_stop.assert_called_once_with()
            board.post.assert_not_called()

    def test_invalid_geometry_rejected_without_board_access(self):
        for excursion in (-1, 0, 401, float("nan"), float("inf")):
            board = Mock()
            with self.assertRaises(ValueError):
                precondition_rho_stealthchop(board, 4, {}, .11, excursion)
            self.assertEqual(board.mock_calls, [])


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


class CruiseGuardTest(unittest.TestCase):
    def setUp(self):
        self.guard = CruiseDriverGuard()
        self.driver = {
            "uartResponseValid": True, "setupOk": True,
            "settings": {"chopconfReadValid": True, "softwareEnabled": True,
                         "chopperOffTime": 3},
            "dynamic": {"stallGuardValid": True, "stallGuardResult": 0},
            "inputs": {"enableN": False},
        }

    def observe(self, phase="during-cruise"):
        self.guard.observe("rho", self.driver, phase)

    def test_verify_cruise_phase_preserves_sustained_sg_guard(self):
        for velocity in (2, -2, 1.8):
            self.guard = CruiseDriverGuard()
            phase = motion_driver_poll_phase("rho", velocity, 2)
            self.assertEqual(phase, "during-cruise")
            self.observe(phase)
            self.observe(phase)
            with self.assertRaisesRegex(RuntimeError, "possible physical stall"):
                self.observe(phase)

    def test_ramps_theta_and_invalid_target_do_not_claim_rho_cruise(self):
        for axis, velocity, maximum in (("rho", .1, 2), ("theta", 2, 2),
                                        ("rho", 0, 0), ("rho", 2, float("nan")),
                                        ("rho", float("nan"), 2)):
            self.assertEqual(motion_driver_poll_phase(axis, velocity, maximum),
                             "during-motion")

    def test_three_low_samples_abort_but_isolated_zero_does_not(self):
        self.observe()
        self.driver["dynamic"]["stallGuardResult"] = 220
        self.observe()
        self.driver["dynamic"]["stallGuardResult"] = 0
        self.observe()
        self.observe()
        with self.assertRaisesRegex(RuntimeError, "possible physical stall"):
            self.observe()

    def test_idle_resets_consecutive_count(self):
        self.observe()
        self.observe()
        self.observe("after-segment-0mm")
        self.observe()
        self.observe()

    def test_invalid_reads_and_disabled_bridge_fail(self):
        self.driver["dynamic"]["stallGuardValid"] = False
        self.observe()
        self.observe()
        with self.assertRaisesRegex(RuntimeError, "lost three"):
            self.observe()
        self.driver["settings"]["chopperOffTime"] = 0
        with self.assertRaisesRegex(RuntimeError, "bridge"):
            self.observe()

    def test_unused_motor_does_not_need_sg_but_must_stay_disabled(self):
        self.driver.update(driverRole="rhoCompanion", motorConfigured=False,
                           connected=True)
        self.driver["settings"].update(softwareEnabled=False, chopperOffTime=0)
        for _ in range(4):
            self.guard.observe("rhoCompanion", self.driver, "during-cruise")
        self.driver["settings"]["softwareEnabled"] = True
        with self.assertRaisesRegex(RuntimeError, "Unused CW"):
            self.guard.observe("rhoCompanion", self.driver, "during-cruise")

    def test_hardware_disable_and_missing_enable_readback_fail(self):
        self.driver["inputs"]["enableN"] = True
        with self.assertRaisesRegex(RuntimeError, "bridge"):
            self.observe()
        self.driver.pop("inputs")
        with self.assertRaisesRegex(RuntimeError, "bridge"):
            self.observe()

    def test_bridge_checks_also_cover_preflight_and_short_verification(self):
        self.driver["settings"]["chopperOffTime"] = 0
        for phase in ("preflight", "before-motion", "during-motion", "after-motion"):
            with self.assertRaisesRegex(RuntimeError, "bridge"):
                self.observe(phase)

    def test_crc_or_setup_failure_and_thermal_fault_fail(self):
        for field in ("uartResponseValid", "setupOk"):
            self.driver[field] = False
            with self.assertRaisesRegex(RuntimeError, "fault"):
                self.observe()
            self.driver[field] = True
        self.driver["status"] = {"overTempWarning": True}
        with self.assertRaisesRegex(RuntimeError, "fault"):
            self.observe()

    def test_planner_uses_repeat_baseline_without_hiding_new_faults_or_reset(self):
        def repeat(counts, baseline=395):
            return {"initialPlannerUnderruns": baseline, "telemetry": [
                {"planner": {"underruns": count, "maxConsecutiveUnderruns": 0}}
                for count in counts]}
        self.assertTrue(planner_repeat_healthy(repeat([395, 395])))
        self.assertFalse(planner_repeat_healthy(repeat([395, 396])))
        self.assertFalse(planner_repeat_healthy(repeat([395, 0])))
        self.assertFalse(planner_repeat_healthy(repeat([])))
        self.assertFalse(planner_repeat_healthy(repeat([0, 395], baseline=0)))


class RhoRampTest(unittest.TestCase):
    def test_offset_screen_visits_hotspot_and_returns_to_original_zero(self):
        targets = rho_segment_targets("screen", 50, 50)
        self.assertEqual(targets, (50, 100, 50, 100, 50, 0))
        self.assertEqual(rho_profile_distance_mm("screen", 50, 50), 300)
        self.assertGreaterEqual(min(targets), 0)
        self.assertEqual(max(targets), 100)
        with self.assertRaises(ValueError):
            rho_segment_targets("range", 350, 50)

    def test_repeated_verification_is_not_qualification(self):
        summary = {"verify": {"provisionalScreen": False,
                              "qualificationEligible": False,
                              "nearField": {"qualificationMeasurementValid": True}}}
        self.assertFalse(repeat_confirmation_satisfied(summary, 2))
        summary["verify"].pop("qualificationEligible")
        self.assertFalse(repeat_confirmation_satisfied(summary, 2))

    def test_spatial_range_covers_whole_stroke_and_returns_to_zero(self):
        targets = rho_segment_targets("range", 400)
        self.assertEqual(targets, (50, 100, 150, 200, 250, 300, 350, 400,
                                   350, 300, 250, 200, 150, 100, 50, 0))
        self.assertEqual(rho_profile_distance_mm("range", 400), 800)
        for excursion in (1, 50, 100, 400):
            targets = rho_segment_targets("range", excursion)
            self.assertEqual(targets[-1], 0)
            self.assertEqual(max(targets), excursion)
            self.assertGreaterEqual(min(targets), 0)
            self.assertEqual(len(targets), 16)

    def test_ramp_returns_each_leg_to_start_within_requested_envelope(self):
        self.assertEqual(rho_segment_targets("ramp", 50), (5, 0, 10, 0, 20, 0, 50, 0))
        self.assertEqual(rho_profile_distance_mm("ramp", 50), 170)
        for excursion in (1, 50, 100, 400):
            targets = rho_segment_targets("ramp", excursion)
            self.assertEqual(targets[1::2], (0, 0, 0, 0))
            self.assertTrue(all(0 <= target <= excursion for target in targets))

    def test_existing_segment_distances_unchanged(self):
        for profile, distance in (("verify", 100), ("screen", 200), ("gated", 400)):
            self.assertEqual(rho_profile_distance_mm(profile, 50), distance)


@unittest.skipUnless(importlib.util.find_spec("matplotlib"), "optional plotting dependency")
class TimingPlotTest(unittest.TestCase):
    def test_shared_time_has_same_horizontal_position_in_all_panels(self):
        import numpy as np
        from matplotlib.figure import Figure
        positions = []

        def inspect_layout(figure, *args, **kwargs):
            figure.canvas.draw()
            positions.extend(axis.get_position().bounds for axis in figure.axes[:3])

        analysis = {"times": np.array([0., 1., 2.]),
                    "frequencies": np.array([100., 1000., 10000.]),
                    "spectra": np.ones((3, 3)) * 1e-8}
        metrics = SimpleNamespace(sample_rate_hz=48000, timing_locked_tones=[],
                                  actual_motion_start_s=0.5, actual_motion_end_s=1.5)
        telemetry = [{"hostOffsetS": t, "velocity": {"rho": v}}
                     for t, v in ((0, 0), (1, 5), (2, 0))]
        with patch.object(Figure, "savefig", inspect_layout):
            save_timing_plot(Path("unused-test-output.png"), analysis, telemetry,
                             metrics, AXES["rho"])
        self.assertEqual(len(positions), 3)
        for position in positions[1:]:
            self.assertAlmostEqual(position[0], positions[0][0], places=6)
            self.assertAlmostEqual(position[2], positions[0][2], places=6)


if __name__ == "__main__":
    unittest.main()
