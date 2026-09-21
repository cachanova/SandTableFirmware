"""Run production homing orchestration and jog recovery against a fake UART bus."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def method(source, name):
    pos = source.index('PolarControl::' + name + '(')
    return source[source.rfind('\n', 0, pos) + 1:source.index('\n}', pos) + 2]


class OptionalRhoDriverTest(unittest.TestCase):
    def test_absent_present_and_failed_companion(self):
        source = (ROOT / 'lib/PolarControl/src/PolarControl.cpp').read_text()
        header = (ROOT / 'lib/PolarControl/src/PolarControl.hpp').read_text()
        settings = header[header.index('struct DriverSettings {'):]
        settings = settings[:settings.index('\n};') + 3]
        methods = [method(source, name) for name in [
            'disableRhoDriversLocked', 'prepareRhoDriversForManualJogLocked',
            'homeDrivers']]
        axis = method(source, 'homeAxis')
        # Exercise real driver selection and cleanup; replace only the physical
        # travel/contact loop with a deterministic successful or failed contact.
        signature = axis[:axis.index('{') + 1]
        selection = axis[axis.index('    const bool inactivePresent ='):
                         axis.index('    bool axisSuccess = false;')]
        cleanup = axis[axis.index('axis_cleanup:'):]
        methods.append(signature + '\nuint32_t inactivePhaseRegister = 0;\n'
                       'constexpr uint32_t kSettleMs = 150;\n' + selection +
                       '\norder.push_back(activeAddress);\n'
                       'bool axisSuccess = failAxis != activeAddress;\n'
                       'if (cancelAfterContact) m_state = INITIALIZED;\n' + cleanup)
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            (directory / 'settings.inc').write_text(settings)
            (directory / 'controller_methods.inc').write_text('\n\n'.join(methods))
            for disabled in [False, True]:
                flags = ['-DTEST_CW_DISABLED'] if disabled else []
                result = subprocess.run(['g++', '-std=c++17', '-I' + tmp, *flags,
                    str(ROOT / 'test/support/rho_optional/harness.cpp'),
                    '-o', tmp + '/optional'], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([tmp + '/optional'], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
