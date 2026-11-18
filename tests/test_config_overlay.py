import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ConfigOverlayTests(unittest.TestCase):
    def test_every_overlay_string_copy_is_bounded_and_terminated(self):
        source = (ROOT / 'src/config_manager.cpp').read_text()
        start = source.index('static void config_check_littlefs_overlay()')
        body = source[start:source.index('\n}\n', start)]
        copies = re.findall(r'strncpy\(config\.(\w+), [^;]*sizeof\(config\.(\w+)\)( - 1)?\)', body)
        self.assertGreaterEqual(len(copies), 10)
        for destination, sized, short in copies:
            self.assertEqual(destination, sized)
            self.assertEqual(short, ' - 1', f'{destination} copy may fill the whole buffer')
            self.assertIn(f"config.{destination}[sizeof(config.{destination}) - 1] = '\\0';", body,
                          f'{destination} is not terminated after the copy')


if __name__ == '__main__':
    unittest.main()
