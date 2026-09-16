import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from acoustic_tuner import stationary_background_metrics


class StationaryBackgroundTests(unittest.TestCase):
    def test_steady_tone_has_consistent_window_levels(self):
        rate = 48000
        times = np.arange(rate * 2) / rate
        metrics = stationary_background_metrics(.001 * np.sin(2 * np.pi * 1000 * times), rate)
        quantiles = metrics["aWeightedDbfsQuantiles"]
        self.assertLess(quantiles["max"] - quantiles["p10"], .1)
        self.assertAlmostEqual(quantiles["p50"], -63.01, delta=.1)
        self.assertFalse(metrics["qualificationEligible"])
        self.assertAlmostEqual(metrics["windowDurationS"], 4096 / rate)

    def test_intermittent_noise_is_retained(self):
        rate = 48000
        times = np.arange(rate * 4) / rate
        audio = .0001 * np.sin(2 * np.pi * 1000 * times)
        audio[rate:rate + rate // 2] *= 100
        metrics = stationary_background_metrics(audio, rate)
        quantiles = metrics["aWeightedDbfsQuantiles"]
        self.assertGreater(quantiles["p95"] - quantiles["p50"], 35)
        self.assertGreater(metrics["meanWindowPowerAWeightedDbfs"] - quantiles["p50"], 25)
        self.assertEqual(metrics["clippedSampleFraction"], 0)

    def test_invalid_input_is_rejected(self):
        for audio, rate in ((np.zeros(100), 48000), (np.zeros(4096), 0),
                            (np.full(4096, np.nan), 48000)):
            with self.subTest(rate=rate, length=len(audio)):
                with self.assertRaises(ValueError):
                    stationary_background_metrics(audio, rate)


if __name__ == "__main__":
    unittest.main()
