"""The load qualifier must reject old low-water failures as well as new ones."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'presence_stress', Path(__file__).resolve().parents[1] / 'scripts/presence_stress.py')
stress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stress)


class ReserveQualification(unittest.TestCase):
    def info(self, **values):
        return dict(heap=78000, largestFreeBlock=36000, heap8Bit=38000,
                    largestFree8BitBlock=32000, minimumFree8BitHeap=values.get('minimum', 8192))

    def test_reserve_boundary(self):
        stress.check_memory(self.info())
        with self.assertRaisesRegex(AssertionError, 'reboot before retesting'):
            stress.check_memory(self.info(minimum=8191))

    def test_recovered_current_heap_does_not_hide_old_failure(self):
        with self.assertRaisesRegex(AssertionError, 'transient byte-heap'):
            stress.check_memory(self.info(minimum=2616))

    def test_byte_heap_and_fragmentation_are_independent_guards(self):
        for key, value, message in [('heap8Bit', 8191, 'heap reserve'),
                                    ('largestFree8BitBlock', 4095, 'fragmentation')]:
            info = self.info()
            info[key] = value
            with self.assertRaisesRegex(AssertionError, message):
                stress.check_memory(info)


if __name__ == '__main__':
    unittest.main()
