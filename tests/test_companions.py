import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CompanionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        temp = Path(cls.temp.name)
        (temp / 'Arduino.h').write_text('#include <stdint.h>\n#include <stdio.h>\nunsigned long millis();\n')
        cls.binary = str(temp / 'companions')
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-I'+str(temp), '-Iinclude',
                        'tests/test_companions.cpp', 'src/ambient_fish.cpp', '-o', cls.binary], cwd=ROOT, check=True)

    def test_seven_pets_bounds_health_priority_and_interaction(self):
        subprocess.run([self.binary], check=True)

    def test_shared_mobile_artwork_fixtures_match_firmware(self):
        expected = json.loads((ROOT / 'protocol/fixtures/companions.json').read_text())
        actual = json.loads(subprocess.check_output([self.binary, 'frames'], text=True))
        self.assertEqual(actual, expected)

    def test_canonical_config_legacy_precedence_and_nvs_migration(self):
        source = (ROOT / 'src/config_manager.cpp').read_text()
        temp = Path(self.temp.name)
        load = next(line for line in source.splitlines() if 'config.ambient_creature = companion_or_default(prefs.getInt' in line)
        save = '\n'.join(line for line in source.splitlines() if 'prefs.putInt("pal_type"' in line or 'prefs.putInt("amb_kind"' in line)
        (temp / 'companion_load.inc').write_text(load)
        (temp / 'companion_save.inc').write_text(save)
        web = (ROOT / 'src/web_server.cpp').read_text()
        (temp / 'companion_web.inc').write_text('\n'.join(line for line in web.splitlines()
            if 'doc["ambient_character"] =' in line or 'doc["ambient_creature"] =' in line))
        binary = str(temp / 'companion-config')
        subprocess.run(['c++', '-std=c++17', '-I'+str(temp), '-Iinclude', '-I.pio/libdeps/esp32dev/ArduinoJson/src',
                        'tests/test_companion_config.cpp', 'src/config_patch.cpp', '-o', binary], cwd=ROOT, check=True)
        subprocess.run([binary], check=True)

    def test_installer_has_identical_companion_assets(self):
        bundled = ROOT / 'onboarding/TC001Setup/TC001Setup/Resources/WebUI/www'
        for name in ('companions.js', 'index.html'):
            self.assertEqual((ROOT / 'data/www' / name).read_bytes(), (bundled / name).read_bytes())
