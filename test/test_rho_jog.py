"""Exercise production independent-jog lifecycle with a fake planner and UART bus."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class RhoJogTest(unittest.TestCase):
    def test_controller_lifecycle(self):
        source = (ROOT / 'lib/PolarControl/src/PolarControl.cpp').read_text()
        names = ['sampleRhoPositionLocked', 'getRhoPositionEstimate', 'jogRelative', 'startInactiveRhoHoldLocked',
                 'serviceInactiveRhoHoldLocked', 'restoreInactiveRhoInterfaceLocked',
                 'finishIndependentRhoJogLocked', 'clearInactiveRhoHoldLocked',
                 'processNextMove', 'stop', 'emergencyStop']
        methods = []
        for name in names:
            pos = source.index('PolarControl::' + name + '(')
            start = source.rfind('\n', 0, pos) + 1
            end = source.index('\n}', pos) + 2
            methods.append(source[start:end])
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            (directory / 'controller_methods.inc').write_text('\n\n'.join(methods))
            result = subprocess.run(['g++', '-std=c++17', '-I'+tmp,
                str(ROOT / 'test/support/rho_jog/harness.cpp'), '-o', tmp+'/jog'],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([tmp+'/jog'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == '__main__':
    unittest.main()
