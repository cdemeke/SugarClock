import os
from pathlib import Path
import shutil
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
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                        '-I', str(temp), '-I', str(ROOT / 'include'),
                        str(ROOT / 'tests/test_companions.cpp'), str(ROOT / 'src/ambient_fish.cpp'),
                        '-o', cls.binary], check=True, capture_output=True)

    def test_urgent_missing_sleep_and_interaction(self):
        subprocess.run([self.binary], check=True)

    @unittest.skipUnless(shutil.which('node'), 'Node required for firmware/preview parity')
    def test_preview_matches_firmware_pixels(self):
        firmware = subprocess.check_output([self.binary, 'frames'], text=True)
        script = '''
require(process.argv[1]);
for(let style=0;style<3;style++)for(let range=0;range<3;range++)
for(let id=0;id<4;id++)for(let mood=0;mood<3;mood++)for(let ms=0;ms<15000;ms+=100)
    console.log(PixelCompanions.frame(id,ms,mood===1,mood===2,style,range).flat().join(''));
'''
        preview = subprocess.check_output(['node', '-e', script, str(ROOT / 'data/www/companions.js')], text=True)
        self.assertEqual(len(firmware), len(preview))
        for index, (expected, actual) in enumerate(zip(firmware.splitlines(), preview.splitlines())):
            self.assertEqual(expected, actual, f"Preview frame {index}")

    def test_installer_has_matching_companion_assets(self):
        bundled = ROOT / 'onboarding/TC001Setup/TC001Setup/Resources/WebUI/www'
        for name in ('companions.js', 'index.html', 'style.css', 'display.html'):
            self.assertEqual((ROOT / 'data/www' / name).read_bytes(), (bundled / name).read_bytes(), name)
