"""STEP audio timing stays anchored across long runs and micros rollover."""
import copy
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from acoustic_tuner import AXES, moving_gap_interpolation_safe, step_motion_intervals


WRAP = 1 << 32


def sample(host, base_us=100_000_000, start=2.0, stop=5.0, epoch=1):
    clock = (base_us + round(host * 1_000_000)) % WRAP
    active = start <= host < stop
    return {
        "hostOffsetS": host, "hostRequestRttS": .02,
        "micros": clock, "millis": clock // 1000,
        "state": "RUNNING" if active else "IDLE",
        "velocity": {"rho": 2.0 if active else 0.0, "theta": 0.0},
        "position": {"rho": min(max(host - start, 0), stop - start) * 2, "theta": 0.0},
        "stepMotion": {
            "epoch": epoch if host >= start else epoch - 1,
            "startMicros": (base_us + round(start * 1_000_000)) % WRAP if host >= start else 0,
            "stopMicros": (base_us + round(stop * 1_000_000)) % WRAP if host >= stop else 0,
            "active": active,
        },
    }


class StepClockRolloverTest(unittest.TestCase):
    def test_long_epoch_beyond_half_and_full_wrap(self):
        for duration in (2975.0, 5000.0):
            stop = 2 + duration
            times = [0.0, 1.9, 2.1, *np.arange(10.0, stop, 10.0), stop + .1, stop + .2]
            trace = [sample(float(t), base_us=WRAP - 5_000_000, stop=stop) for t in times]
            # A much later low-RTT sample must not relocate the old start.
            trace[-1]["hostRequestRttS"] = .001
            intervals = step_motion_intervals(trace)
            self.assertEqual(len(intervals), 1)
            self.assertAlmostEqual(intervals[0]["startHostS"], 2)
            self.assertAlmostEqual(intervals[0]["stopHostS"], stop)
            self.assertEqual(intervals[0]["startUncertaintyS"], .01)

    def test_zero_timestamp_is_valid_at_rollover(self):
        for base_us in (WRAP - 2_000_000, WRAP - 5_000_000):
            trace = [sample(float(t), base_us=base_us) for t in np.arange(0, 6, .1)]
            intervals = step_motion_intervals(trace)
            self.assertEqual(len(intervals), 1)
            self.assertAlmostEqual(intervals[0]["startHostS"], 2)
            self.assertAlmostEqual(intervals[0]["stopHostS"], 5)

    def test_long_epoch_clock_rate_skew_uses_local_boundary_mapping(self):
        stop = 2977.0
        rate = 1.000020
        base = WRAP - 5_000_000
        times = [0.0, 1.9, 2.1, *np.arange(10.0, stop, 10.0), stop + .1, stop + .2]
        trace = [sample(float(t), base_us=base, stop=stop) for t in times]
        for row in trace:
            t = row["hostOffsetS"]
            row["micros"] = (base + round(t * rate * 1_000_000)) % WRAP
            row["millis"] = row["micros"] // 1000
            if t >= 2:
                row["stepMotion"]["startMicros"] = (base + round(2 * rate * 1_000_000)) % WRAP
            if t >= stop:
                row["stepMotion"]["stopMicros"] = (base + round(stop * rate * 1_000_000)) % WRAP
        trace[-1]["hostRequestRttS"] = .001
        interval = step_motion_intervals(trace)[0]
        self.assertAlmostEqual(interval["startHostS"], 2, delta=.000010)
        self.assertAlmostEqual(interval["stopHostS"], stop, delta=.000010)
        self.assertEqual(interval["startUncertaintyS"], .01)
        self.assertEqual(interval["stopUncertaintyS"], .0005)

    def test_stale_prior_epoch_is_filtered_with_continuous_clock(self):
        trace = [sample(float(t), epoch=8) for t in np.arange(0, 6, .1)]
        for row in trace:
            if row["hostOffsetS"] < 2:
                row["stepMotion"] = dict(epoch=7, active=False,
                                         startMicros=95_000_000, stopMicros=98_000_000)
        intervals = step_motion_intervals(trace)
        self.assertEqual([item["epoch"] for item in intervals], [8])
        self.assertAlmostEqual(intervals[0]["startHostS"], 2)

    def test_normal_two_gate_trace_preserves_epoch_boundaries(self):
        trace = [sample(float(t)) for t in np.arange(0, 8, .1)]
        for index, row in enumerate(trace):
            if row["hostOffsetS"] >= 6:
                trace[index] = sample(row["hostOffsetS"], start=6, stop=7, epoch=2)
        intervals = step_motion_intervals(trace)
        self.assertEqual([item["epoch"] for item in intervals], [1, 2])
        self.assertEqual([(round(item["startHostS"], 3), round(item["stopHostS"], 3))
                          for item in intervals], [(2, 5), (6, 7)])
        # A reused old epoch must not bypass the first-seen epoch checks.
        trace[-1]["stepMotion"] = copy.deepcopy(trace[55]["stepMotion"])
        with self.assertRaisesRegex(ValueError, "regressed"):
            step_motion_intervals(trace)

    def test_reset_host_jump_partial_clock_and_outlier_fail_closed(self):
        original = [sample(float(t)) for t in np.arange(0, 6, .1)]
        for corruption in ("reset", "host", "missing", "micros", "start", "stop", "epoch", "missing_event"):
            trace = copy.deepcopy(original)
            if corruption == "reset":
                trace[30]["micros"] = 0
            elif corruption == "host":
                trace[30]["hostOffsetS"] += 1
            elif corruption == "missing":
                del trace[30]["micros"]
            elif corruption == "micros":
                trace[30]["micros"] += 1_000_000
            elif corruption == "start":
                trace[30]["stepMotion"]["startMicros"] += 1_000_000
            elif corruption == "stop":
                trace[-1]["stepMotion"]["stopMicros"] += 1_000_000
            elif corruption == "missing_event":
                del trace[30]["stepMotion"]["startMicros"]
            else:
                trace[30]["stepMotion"]["epoch"] = 0
            with self.subTest(corruption=corruption), self.assertRaises(ValueError):
                step_motion_intervals(trace)

    def test_ambiguous_sampling_gap_and_all_legacy_clock(self):
        trace = [sample(0), sample(2200, stop=2300)]
        with self.assertRaises(ValueError):
            step_motion_intervals(trace)
        self.assertEqual(step_motion_intervals([dict(hostOffsetS=0)]), [])


class MovingGapClockTest(unittest.TestCase):
    def test_derived_millis_wrap_uses_correct_microsecond_modulus(self):
        before = sample(3, base_us=WRAP - 3_200_000)
        after = sample(3.4, base_us=WRAP - 3_200_000)
        self.assertLess(after["millis"], before["millis"])
        self.assertTrue(moving_gap_interpolation_safe(before, after, AXES["rho"], .002))

    def test_reset_host_mismatch_missing_clock_and_direction_change_rejected(self):
        before = sample(3)
        original_after = sample(3.4)
        for corruption in ("reset", "host", "missing", "reverse", "idle", "position"):
            after = copy.deepcopy(original_after)
            if corruption == "reset":
                after["micros"] = 0
            elif corruption == "host":
                after["hostOffsetS"] += 1
            elif corruption == "missing":
                del after["micros"]
            elif corruption == "reverse":
                after["velocity"]["rho"] = -2
            elif corruption == "idle":
                after["state"] = "IDLE"
            else:
                after["position"]["rho"] = before["position"]["rho"]
            with self.subTest(corruption=corruption):
                self.assertFalse(moving_gap_interpolation_safe(before, after, AXES["rho"], .002))


if __name__ == "__main__":
    unittest.main()
