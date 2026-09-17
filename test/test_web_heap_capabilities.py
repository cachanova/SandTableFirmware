"""Regression checks for memory-pool selection at bulk allocation gates."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WebHeapCapabilities(unittest.TestCase):
    def test_image_admission_uses_byte_addressable_pool(self):
        source = (ROOT / 'lib/WebServer/src/PatternImageResponse.hpp').read_text()
        constructor = source.split('PatternImageResponse(const String&', 1)[1].split(
            '~PatternImageResponse()', 1)[0]
        self.assertNotIn('ESP.getFreeHeap()', constructor)
        self.assertIn('heap_caps_get_free_size(MALLOC_CAP_8BIT) < 24576', constructor)
        self.assertIn('heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 8192', constructor)

    def test_index_admission_uses_byte_addressable_pool(self):
        source = (ROOT / 'lib/WebServer/src/SisyphusWebServer.cpp').read_text()
        index = source.split('bool SisyphusWebServer::updateFileListCache()', 1)[1].split(
            'const SisyphusWebServer::FileEntry*', 1)[0]
        self.assertNotIn('ESP.getFreeHeap()', index)
        self.assertIn('heap_caps_get_free_size(MALLOC_CAP_8BIT) < 24576', index)


if __name__ == '__main__':
    unittest.main()
