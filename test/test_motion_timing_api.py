"""Exercise the fixed printf JSON contract without a board or webserver."""
import ast
import json
from pathlib import Path
import re
import unittest


class StepTimingJsonTest(unittest.TestCase):
    def test_axis_summary_serializes_zero_rollover_and_invalid_snapshot(self):
        path = Path(__file__).resolve().parents[1] / "lib/WebServer/src/SisyphusWebServer.cpp"
        source = path.read_text()
        helper = source.split("static void writeAxisStepTiming(", 1)[1].split(
            "void SisyphusWebServer::handleMotionTelemetry", 1
        )[0]
        match = re.search(r'out\.printf\(\s*((?:"(?:\\.|[^"\\])*"\s*)+),', helper)
        self.assertIsNotNone(match)
        template = "".join(ast.literal_eval(value) for value in
                           re.findall(r'"(?:\\.|[^"\\])*"', match.group(1)))
        for valid, counter, interval_valid in (("true", 0, "false"),
                                                ("true", 2**32 - 1, "true"),
                                                ("false", 27, "false")):
            rendered = template % (valid, *([counter] * 9), interval_valid)
            data = json.loads(rendered)
            self.assertEqual(data["valid"], valid == "true")
            self.assertEqual(data["steps"], counter)
            self.assertEqual(data["outliers"], counter)
            self.assertEqual(data["maxLateUs"], counter)
            self.assertEqual(data["maxIntervalErrorUs"], counter)
            self.assertEqual(data["last"]["actualMicros"], counter)
            self.assertEqual(data["last"]["scheduledMicros"], counter)
            self.assertEqual(data["last"]["actualIntervalUs"], counter)
            self.assertEqual(data["last"]["plannedIntervalUs"], counter)
            self.assertEqual(data["last"]["intervalValid"], interval_valid == "true")


if __name__ == "__main__":
    unittest.main()
