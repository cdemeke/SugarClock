"""Nightscout address, entry and display-unit regression tests."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NightscoutTests(unittest.TestCase):
    def test_addresses_entries_freshness_and_units(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp) / 'nightscout')
            subprocess.run([
                os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                '-I', str(ROOT / 'tests/glucose_display_stubs'),
                '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/test_nightscout.cpp'),
                str(ROOT / 'src/trend_mapping.cpp'), '-o', binary,
            ], check=True)
            subprocess.run([binary], check=True)

    def test_settings_page_offers_nightscout(self):
        source = (ROOT / 'data/www/index.html').read_text()
        self.assertIn('<option value="4" data-i18n="Nightscout">Nightscout</option>', source)
        # Nightscout reuses the address and token fields stored as server_url/auth_token.
        self.assertIn("getElementById('custom-fields').style.display = src === '0' || src === '4'", source)
        self.assertIn("getElementById('nightscout-hint').style.display = src === '4'", source)
        # GET /api/config never returns the token itself, only whether one is saved.
        self.assertNotIn('c.auth_token', source)
        self.assertIn('c.has_auth_token', source)


if __name__ == '__main__':
    unittest.main()
