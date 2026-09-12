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

    def test_visible_summary_age_and_zero_delta(self):
        html = (ROOT / 'data/www/display.html').read_text()
        self.assertIn('<section class="glucose-summary" aria-labelledby="glucose-summary-heading">', html)
        summary = html.split('<section class="glucose-summary"', 1)[1].split('</section>', 1)[0]
        self.assertNotIn('visually-hidden', summary)
        script = r'''
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
require(process.argv[1]);
const html=fs.readFileSync(process.argv[2],'utf8');
const nodes={};global.document={getElementById:id=>nodes[id]||(nodes[id]={})};
const source=html.slice(html.indexOf('let readingUnits = null;'),html.indexOf('async function refresh()'));
vm.runInThisContext(`const trendMap={Flat:'→'}, colorMap={green:'color-green',gray:'color-gray'};
function fmtAge(s){return s+'s ago';}\n`+source+`
readingUnits=true;latestReading={valid:true,glucose:130,delta:0,trend:'Flat',color:'green',data_age_sec:42};renderReading();`);
assert.equal(nodes['glucose-value'].textContent,'7.2');
assert.equal(nodes['delta-display'].textContent,'+0.0 mmol/L');
assert.equal(nodes['data-age'].textContent,'Updated 42s ago');
vm.runInThisContext("latestReading.color='gray';renderReading()");
assert.equal(nodes['data-age'].textContent,'Stale reading · 42s ago');
vm.runInThisContext('latestReading.data_age_sec=-1;renderReading()');
assert.equal(nodes['data-age'].textContent,'Reading age unavailable');
vm.runInThisContext('latestReading.valid=false;renderReading()');
assert.equal(nodes['data-age'].textContent,'No glucose reading');
assert.equal(nodes['delta-display'].textContent,'');
vm.runInThisContext('latestReading=null;renderReading()');
assert.equal(nodes['data-age'].textContent,'Reading unavailable');
'''
        subprocess.run(['node', '-e', script, str(ROOT / 'data/www/glucose-format.js'),
                        str(ROOT / 'data/www/display.html')], check=True)
