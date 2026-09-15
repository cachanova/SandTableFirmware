"""A guard failure must stop capture and retain evidence, not qualify/return."""
import argparse
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import acoustic_tuner as tuner


class FailedTrialArtifactTest(unittest.TestCase):
    def run_failure(self, directory, *, bad_snapshot=False, cleanup_fails=False,
                    writer_fails=False, recorder_start_fails=False):
        events = []
        now = [100.0]
        guard_error = RuntimeError("rho: three SG <=5 samples, ledger position uncertain")
        startup_error = RuntimeError("recorder startup failed")
        args = argparse.Namespace(axis="rho", rho_window_start_mm=0,
            rho_excursion_mm=10, theta_excursion_deg=10, settle=0,
            source="fake-microphone", rate=48000, pre_idle=0, post_idle=0,
            duration=5, telemetry_interval=.01, motion_velocity_threshold=.002,
            gated_idle=0, lightweight_driver_polling=True)

        class FakeBoard:
            stops = 0

            def recovering_stop(self):
                self.stops += 1
                events.append("board-stop")
                if cleanup_fails and self.stops > 1:
                    raise RuntimeError("stop response lost")

            def post(self, path, parameters=None):
                events.append(("post", path, parameters))

            def get(self, path):
                events.append(("get", path))
                return {"snapshotKind": "wrong-kind" if bad_snapshot else "motion-health",
                        "dynamic": {"stallGuardValid": True, "stallGuardResult": 0}}

        class FakeRecorder:
            audio_start_monotonic = None
            audio_end_monotonic = None
            timestamp_uncertainty_s = .012
            priming_blocks_discarded = 3

            def __init__(self, *unused):
                pass

            def start(self):
                events.append("recorder-start")
                self.audio_start_monotonic = now[0] + .1
                if recorder_start_fails:
                    raise startup_error

            def stop(self):
                events.append("recorder-stop")
                self.audio_end_monotonic = now[0]
                if cleanup_fails:
                    raise RuntimeError("recorder finalization failed")

        calls = [0]

        def telemetry(*unused, **kwargs):
            calls[0] += 1
            active = calls[0] > 1
            return dict(state="RUNNING" if active else "IDLE", micros=100000,
                hostOffsetS=now[0] - 100, hostRequestRttS=.01,
                position=dict(rho=2.5125 if active else 0, theta=0),
                velocity=dict(rho=1 if active else 0, theta=0),
                planner=dict(underruns=0), stepMotion=dict(epoch=int(active)))

        def observe(role, driver, phase):
            if phase.startswith("during-"):
                raise guard_error

        real_writer = tuner._write_failed_timed_repeat

        def writer(*arguments):
            self.assertEqual(events[-1], "recorder-stop")
            events.append("write-failure")
            if writer_fails:
                raise OSError("disk unavailable")
            real_writer(*arguments)

        with patch.object(tuner.time, "monotonic", side_effect=lambda: now[0]), \
                patch.object(tuner.time, "sleep", side_effect=lambda delay: now.__setitem__(0, now[0] + delay)), \
                patch.object(tuner, "_telemetry_sample", side_effect=telemetry), \
                patch.object(tuner, "microphone_metadata", return_value={"gain": "unchanged"}), \
                patch.object(tuner, "TimestampedRecorder", FakeRecorder), \
                patch.object(tuner.CruiseDriverGuard, "observe", side_effect=observe), \
                patch.object(tuner, "_write_failed_timed_repeat", side_effect=writer), \
                patch.object(tuner, "analyze_timed") as analyze:
            with self.assertRaises(RuntimeError) as caught:
                tuner.run_timed_repeat(FakeBoard(), args, Path(directory), "trial-r1", "verify", 1,
                    settings_reference={"tuning": {"motion": {"rMaxVelocity": 1}},
                                        "driverSettings": {"runCurrent": 200}})
            if not bad_snapshot:
                self.assertIs(caught.exception, startup_error if recorder_start_fails else guard_error)
            analyze.assert_not_called()
        posts = [event for event in events if isinstance(event, tuple) and event[0] == "post"]
        if not recorder_start_fails:
            self.assertEqual(len(posts), 1)
            self.assertEqual(posts[0][2], {"targetMm": 10})
        else:
            self.assertFalse(posts)
        # No target0 / automatic inward movement after the guard.
        self.assertFalse(any(event[2] == {"targetMm": 0} for event in posts))
        return events

    def test_guard_failure_preserves_trace_origin_settings_after_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            self.run_failure(directory)
            failed = json.loads((Path(directory) / "trial-r1-verify-failed.json").read_text())
            timeline = json.loads((Path(directory) / "trial-r1-verify-timeline.json").read_text())
            self.assertEqual(failed, timeline)
            self.assertFalse(failed["testCompletedNaturally"])
            self.assertFalse(failed["qualificationEligible"])
            self.assertNotIn("summary", failed)
            self.assertEqual(failed["lastObservedSample"]["position"]["rho"], 2.5125)
            self.assertEqual(len(failed["driverSamples"]), 3)
            self.assertEqual(failed["driverSamples"][-1]["samplePhase"], "during-cruise")
            self.assertIn("requestStartHostS", failed["driverSamples"][-1])
            self.assertEqual(failed["recorderTimestampUncertaintyS"], .012)
            self.assertAlmostEqual(failed["recorderLaunchOffsetS"], .1)
            self.assertIsNotNone(failed["commandOffsetS"])
            self.assertIsNotNone(failed["stopRequestOffsetS"])
            self.assertEqual(failed["cleanup"]["boardStop"], "completed")
            self.assertEqual(failed["cleanup"]["recorderStop"], "completed")
            self.assertEqual(failed["settingsReference"]["driverSettings"]["runCurrent"], 200)

    def test_stop_and_recorder_errors_do_not_mask_guard_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            self.run_failure(directory, cleanup_fails=True)
            failed = json.loads((Path(directory) / "trial-r1-verify-failed.json").read_text())
            self.assertIn("three SG", failed["error"]["message"])
            self.assertEqual(len(failed["cleanup"]["errors"]), 2)

    def test_artifact_write_failure_does_not_mask_original_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            self.run_failure(directory, writer_fails=True, cleanup_fails=True)

    def test_recorder_start_failure_still_cleans_up_and_writes_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            self.run_failure(directory, recorder_start_fails=True)
            failed = json.loads((Path(directory) / "trial-r1-verify-failed.json").read_text())
            self.assertEqual(failed["failureStage"], "recorder-start")
            self.assertEqual(failed["telemetry"], [])
            self.assertEqual(failed["startSample"]["state"], "IDLE")
            self.assertEqual(failed["commandRequests"], [])

    def test_wrong_snapshot_kind_retains_the_failed_response(self):
        with tempfile.TemporaryDirectory() as directory:
            self.run_failure(directory, bad_snapshot=True)
            failed = json.loads((Path(directory) / "trial-r1-verify-failed.json").read_text())
            self.assertEqual(failed["driverSamples"][-1]["snapshotKind"], "wrong-kind")
            self.assertIn("motion-health", failed["error"]["message"])

    def test_post_capture_analysis_failure_keeps_original_after_one_cleanup(self):
        failure = ValueError("FFT analysis rejected timing")
        board, recorder = Mock(), Mock()
        recorder.audio_start_monotonic = 10.1
        recorder.audio_end_monotonic = 20.1
        recorder.timestamp_uncertainty_s = .01
        recorder.priming_blocks_discarded = 3

        def impl(*arguments):
            context, cleanup = arguments[-2:]
            context.update(recorder=recorder, recordingZeroMonotonic=10,
                           completedSequence=True, stage="post-capture-analysis")
            cleanup()
            raise failure

        with tempfile.TemporaryDirectory() as directory, \
                patch.object(tuner, "_run_timed_repeat_impl", side_effect=impl):
            with self.assertRaises(ValueError) as caught:
                tuner.run_timed_repeat(board, argparse.Namespace(axis="rho"),
                                       Path(directory), "analysis", "verify", 1)
            self.assertIs(caught.exception, failure)
            board.recovering_stop.assert_called_once()
            recorder.stop.assert_called_once()
            failed = json.loads((Path(directory) / "analysis-verify-failed.json").read_text())
            self.assertTrue(failed["motionSequenceCompletedBeforeFailure"])
            self.assertFalse(failed["testCompletedNaturally"])
            self.assertFalse(failed["qualificationEligible"])

    def test_theta_recovery_is_not_attempted_when_stop_failed(self):
        failure = RuntimeError("capture guard")
        board = Mock()
        board.recovering_stop.side_effect = RuntimeError("unconfirmed stop")
        recover = Mock()

        def impl(*arguments):
            context = arguments[-2]
            context.update(recoverThetaOrigin=recover, completedSequence=False)
            raise failure

        with tempfile.TemporaryDirectory() as directory, \
                patch.object(tuner, "_run_timed_repeat_impl", side_effect=impl):
            with self.assertRaises(RuntimeError) as caught:
                tuner.run_timed_repeat(board, argparse.Namespace(axis="theta"),
                                       Path(directory), "theta", "verify", 1)
            self.assertIs(caught.exception, failure)
            recover.assert_not_called()


if __name__ == "__main__":
    unittest.main()
