import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('node'), 'Node required for dashboard formatting')
class DashboardUnitsTests(unittest.TestCase):
    def test_dashboard_value_and_delta_match_firmware(self):
        # Include both signs, rounding boundaries and zero for both unit modes.
        cpp = '''#include <stdio.h>
#include "glucose_format.h"
int main() {
    char text[32];
    for (int mmol=0; mmol<2; ++mmol)
        for (int delta=0; delta<2; ++delta)
            for (int value=-500; value<=600; ++value) {
                if (delta) format_glucose_delta(text, sizeof(text), value, mmol);
                else format_glucose_value(text, sizeof(text), value, mmol);
                puts(text);
            }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'format.cpp'
            binary = Path(tmp) / 'format'
            source.write_text(cpp)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11',
                            '-I', str(ROOT / 'include'), str(source), '-o', str(binary)], check=True)
            expected = subprocess.check_output([str(binary)], text=True)
        script = '''require(process.argv[1]);
for(let mmol=0;mmol<2;mmol++)for(let delta=0;delta<2;delta++)
for(let value=-500;value<=600;value++)console.log(GlucoseFormat.value(value,!!mmol,!!delta));
'''
        actual = subprocess.check_output(['node', '-e', script, str(ROOT / 'data/www/glucose-format.js')], text=True)
        self.assertEqual(expected, actual)

    def test_review_example_and_nonfinite_reading(self):
        script = '''require(process.argv[1]);
const assert=require('node:assert/strict');
assert.equal(GlucoseFormat.value(130,true),'7.2');
assert.equal(GlucoseFormat.value(18,true,true),'+1.0');
assert.equal(GlucoseFormat.value(-18,true,true),'-1.0');
assert.equal(GlucoseFormat.value(130,false),'130');
assert.equal(GlucoseFormat.value(NaN,true),'---');
'''
        subprocess.run(['node', '-e', script, str(ROOT / 'data/www/glucose-format.js')], check=True)
