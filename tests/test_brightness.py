from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('node'), 'Node required for brightness form tests')
class BrightnessTests(unittest.TestCase):
    def test_exact_levels_labels_and_mode_preservation(self):
        # Execute the production form handlers, load logic and save fields.
        script = r'''
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict');
const html = fs.readFileSync(process.argv[1], 'utf8');
const nodes = {};
for (const tag of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)) {
    const attrs = Object.fromEntries([...tag[0].matchAll(/([\w-]+)="([^"]*)"/g)].map(m => [m[1], m[2]]));
    nodes[tag[1]] = {...attrs, checked: false, listeners: {},
        addEventListener(event, fn) { this.listeners[event] = fn; },
        setAttribute(name, value) { this[name] = value; },
        stepDown() { this.value = String(Math.max(+this.min, +this.value - +this.step)); },
        stepUp() { this.value = String(Math.min(+this.max, +this.value + +this.step)); }};
}
global.document = {getElementById: id => nodes[id]};
vm.runInThisContext(html.slice(html.indexOf('let brightnessDeviceValue ='), html.indexOf('// ---- On-device WiFi setup ----')));
const load = html.slice(html.indexOf('brightnessDeviceValue = Math.max'), html.indexOf("document.getElementById('show_delta').checked = c.show_delta"));
const save = html.slice(html.indexOf('brightness: brightnessDeviceValue,'), html.indexOf("show_delta: document.getElementById"));
function loadConfig(brightness, automatic) {
    global.c = {brightness, auto_brightness: automatic};
    vm.runInThisContext(load);
}
function payload() { return vm.runInThisContext('({' + save + '})'); }
const slider = nodes.brightness;
assert.equal(slider.type, 'range');
assert.equal(slider.min, '1');
assert.equal(slider.max, '255');
assert.equal(slider.step, '1');
assert.equal(nodes['brightness-decrease'].type, 'button');
assert.equal(nodes['brightness-increase'].type, 'button');

// No setting is rounded or changes automatic mode on an unrelated save.
for (let value = 1; value <= 255; value++) {
    for (const automatic of [false, true]) {
        loadConfig(value, automatic);
        assert.equal(+slider.value, value);
        assert.deepEqual(payload(), {brightness: value});
        assert.equal(nodes.auto_brightness.checked, automatic);
        slider.listeners.input();
        assert.deepEqual(payload(), {brightness: value, auto_brightness: false});
    }
}

// Former minimum 3 -> 2 -> 1: distinct labels, exact save values, no zero/off.
loadConfig(3, true);
assert.equal(nodes['brightness-value'].textContent, '1.2%');
nodes['brightness-decrease'].listeners.click();
assert.deepEqual(payload(), {brightness: 2, auto_brightness: false});
assert.equal(nodes['brightness-value'].textContent, '0.8%');
nodes['brightness-decrease'].listeners.click();
assert.deepEqual(payload(), {brightness: 1, auto_brightness: false});
assert.equal(nodes['brightness-value'].textContent, '0.4%');
assert.equal(slider['aria-valuetext'], '0.4 percent, minimum');
assert.equal(nodes['brightness-decrease'].disabled, true);
nodes['brightness-decrease'].listeners.click();
assert.equal(payload().brightness, 1);
nodes['brightness-increase'].listeners.click();
assert.equal(payload().brightness, 2);
assert.equal(nodes['brightness-decrease'].disabled, false);

loadConfig(254, false);
nodes['brightness-increase'].listeners.click();
assert.equal(payload().brightness, 255);
assert.equal(nodes['brightness-value'].textContent, '100%');
assert.equal(nodes['brightness-increase'].disabled, true);
nodes['brightness-increase'].listeners.click();
assert.equal(payload().brightness, 255);

// Automatic mode can still be restored without moving the saved slider level.
loadConfig(1, false);
nodes.auto_brightness.checked = true;
nodes.auto_brightness.listeners.change();
assert.deepEqual(payload(), {brightness: 1, auto_brightness: true});
assert.match(nodes['brightness-mode-hint'].textContent, /Automatic brightness is on/);
'''
        subprocess.run(['node', '-e', script, str(ROOT / 'data/www/index.html')], check=True)
