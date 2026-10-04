"""Sensor age and shared display freshness regression tests."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GlucoseFreshnessTests(unittest.TestCase):
    def test_cached_readings_and_display_policy(self):
        source = r'''
#include "glucose_freshness.h"
#include <cassert>
int main() {
    const uint32_t ts = 1780000000;
    // Cached readings already six minutes old on the first successful poll.
    uint32_t anchor = glucose_received_at(ts, 0, false, 0, 100, true, ts + 360);
    assert(uint32_t(100 - anchor) == 360000);
    // Duplicate successful polls do not refresh the age, even without NTP.
    anchor = glucose_received_at(ts, ts, true, anchor, 60100, false, 0);
    assert(uint32_t(60100 - anchor) == 420000);
    // Unsynced boot still ages repeated timestamps beyond five minutes.
    anchor = glucose_received_at(ts, 0, false, 0, 100, false, 0);
    anchor = glucose_received_at(ts, ts, true, anchor, 300101, false, 0);
    assert(uint32_t(300101 - anchor) == 300001);
    // A genuinely newer reading restores fresh color, even at the same value.
    anchor = glucose_received_at(ts + 300, ts, true, anchor, 300101, true, ts + 301);
    assert(uint32_t(300101 - anchor) == 1000);
    // millis rollover and a backward clock correction preserve elapsed age.
    anchor = glucose_received_at(ts, ts, true, 0xfffffff0, 100, true, ts - 1);
    assert(uint32_t(100 - anchor) == 116);
    // A timestamp-less generic endpoint uses receipt time, not a stale identity.
    assert(glucose_received_at(0, ts, true, 0, 500, true, ts) == 500);
    // Extreme old timestamps cannot overflow the age multiplication.
    anchor = glucose_received_at(1, 0, false, 0, 500, true, ts);
    assert(uint32_t(500 - anchor) == 0x7fffffff);
    // Realistic cycle: sensor T arrives at T+90s, next sample T+300s
    // is fetched at T+360s. No gray interval during that delivery gap.
    anchor = glucose_received_at(ts, 0, false, 0, 90000, true, ts + 90);
    for (uint32_t now = 90000; now <= 360000; now += 1000) {
        anchor = glucose_received_at(ts, ts, true, anchor, now, true, ts + now / 1000);
        assert(!glucose_display_is_stale(0, uint32_t(now - anchor), 20, 0));
    }
    anchor = glucose_received_at(ts + 300, ts, true, anchor, 360000, true, ts + 360);
    assert(uint32_t(360000 - anchor) == 60000);
    assert(!glucose_display_is_stale(0, 60000, 20, 0));
    // If the next sample never arrives, cached successful polls still go gray.
    anchor = glucose_received_at(ts + 300, ts + 300, true, anchor, 721000, true, ts + 721);
    assert(glucose_display_is_stale(0, uint32_t(721000 - anchor), 20, 0));
    for (int source = 0; source <= 3; ++source) {
        assert(!glucose_display_is_stale(source, 299999, 5, 0));
        assert(glucose_display_is_stale(source, 300000, 5, 0) == (source != 2));
        assert(glucose_display_is_stale(source, 300001, 5, 0) == (source != 2));
        assert(!glucose_display_is_stale(source, 420000, 20, 0));
        assert(glucose_display_is_stale(source, 420001, 20, 0) == (source != 2));
        assert(glucose_display_is_stale(source, 0, 20, 5) == (source != 2));
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / 'freshness.cpp'
            exe = Path(tmp) / 'freshness'
            cpp.write_text(source)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall',
                            '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
                            str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
