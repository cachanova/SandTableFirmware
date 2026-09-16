"""Source contracts for the operator-adopted profile; no hardware motion."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class AdoptedRhoDefaultsTest(unittest.TestCase):
    def test_motion_defaults_preserve_theta_and_ramps(self):
        header = (ROOT / "lib/PolarControl/src/PolarControl.hpp").read_text()
        motion = header.split("struct MotionSettings {", 1)[1].split("};", 1)[0]
        values = dict(re.findall(r"float\s+(\w+)\s*=\s*([\d.]+)f", motion))
        expected = dict(rMaxVelocity=5.5, rMaxAccel=20, rMaxJerk=100,
                        tMaxVelocity=.225, tMaxAccel=2, tMaxJerk=10)
        self.assertEqual({key: float(value) for key, value in values.items()}, expected)

    def test_rho_factory_registers_match_adopted_candidate(self):
        source = (ROOT / "lib/PolarControl/src/PolarControl.cpp").read_text()
        constructor = source.split("PolarControl::PolarControl() {", 1)[1].split(
            "PolarControl::~PolarControl()", 1)[0]
        values = dict(re.findall(r"m_rDriverSettings\.(\w+)\s*=\s*(\w+)\s*;", constructor))
        expected = dict(runCurrent="350", holdCurrent="350", microsteps="8",
                        highSensitivityCurrentScale="true", blankTime="0",
                        pwmFrequency="2", pwmRegulation="1", pwmLimit="8",
                        automaticCurrentScaling="true", automaticGradientAdaptation="false",
                        pwmOffset="128", pwmGradient="0", coolStepEnabled="true",
                        coolStepLowerThreshold="2", coolStepUpperThreshold="1",
                        coolStepCurrentIncrement="2", coolStepMeasurementCount="0",
                        coolStepThreshold="1000")
        self.assertEqual(values, expected)

    def test_homing_still_disables_coolstep(self):
        source = (ROOT / "lib/PolarControl/src/PolarControl.cpp").read_text()
        self.assertIn("homingSettings.coolStepEnabled = false;", source)


if __name__ == "__main__":
    unittest.main()
