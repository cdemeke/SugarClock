import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cassert>
struct { void println(const char*) {} } Serial;
static bool portal_up = false, connected = false;
static int starts = 0;
static void portal_start() { portal_up = true; ++starts; }
bool wifi_is_connected() { return connected; }
#include "open_portal.inc"
int main() {
    connected = true;
    assert(!wifi_open_setup_portal() && !portal_up);   // working WiFi: nothing to recover
    connected = false;
    assert(wifi_open_setup_portal() && portal_up && starts == 1);
    assert(wifi_open_setup_portal() && starts == 1);   // idempotent
    return 0;
}
'''


class SetupPortalGateTests(unittest.TestCase):
    def test_saved_wifi_never_opens_the_portal_by_itself(self):
        source = (ROOT / 'src/wifi_manager.cpp').read_text()
        loop = source[source.index('void wifi_loop()'):source.index('// ---------------------------------------------------------------------------\n// Accessors')]
        after_unconfigured = loop[loop.index('if (!config_has_wifi())'):]
        after_unconfigured = after_unconfigured[after_unconfigured.index('return;\n    }') :]
        self.assertNotIn('portal_start()', after_unconfigured)
        self.assertNotIn('WIFI_SETUP_AP_AFTER_MS', source)

    def test_button_hold_opens_the_portal_only_while_offline(self):
        source = (ROOT / 'src/wifi_manager.cpp').read_text()
        body = source[source.index('bool wifi_open_setup_portal()'):source.index('// ---------------------------------------------------------------------------\n// Init')]
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / 'open_portal.inc').write_text(body)
            (pathlib.Path(tmp) / 'test.cpp').write_text(HARNESS)
            exe = str(pathlib.Path(tmp) / 'test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + tmp,
                            str(pathlib.Path(tmp) / 'test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)

    def test_recovery_gesture_opens_the_portal(self):
        main = (ROOT / 'src/main.cpp').read_text()
        branch = main[main.index('if (evt == BTN_PAIRING) {'):main.index('} else if ((evt == BTN_MIDDLE_SHORT')]
        self.assertIn('wifi_open_setup_portal();', branch)


if __name__ == '__main__':
    unittest.main()
