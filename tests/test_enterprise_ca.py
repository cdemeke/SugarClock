import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
struct WifiTrialParams { char ssid[64]; int security; int eap_method; char identity[128];
                         char password[128]; char anon_identity[128]; bool validate_ca; };
struct { void println(const char*) {} template<class... A> void printf(const char*, A...) {} } Serial;
#define MAX_CA_PEM 4096
static bool ca_exists = false, ca_set = false, ca_cleared = false, enabled = false;
static size_t ca_size = 0, ca_read = 0;
bool config_ca_exists() { return ca_exists; }
size_t config_ca_size() { return ca_size; }
size_t config_ca_read(char*, size_t) { return ca_read; }
#define EAP_SET_IDENTITY(a, b) (void)(a)
#define EAP_SET_USERNAME(a, b)
#define EAP_SET_PASSWORD(a, b)
#define EAP_SET_TTLS_PHASE2(a)
#define EAP_SET_CA_CERT(a, b) (ca_set = true)
#define EAP_CLEAR_CA_CERT() (ca_cleared = true)
#define EAP_SET_DISABLE_TIME_CHECK(a)
#define EAP_ENABLE() (enabled = true)
#define EAP_DISABLE() (enabled = false)
#include "enterprise_apply.inc"
static bool run(bool validate, bool exists, size_t size, size_t read) {
    ca_exists = exists; ca_size = size; ca_read = read; ca_set = ca_cleared = enabled = false;
    WifiTrialParams p = {}; strcpy(p.identity, "user"); p.validate_ca = validate;
    return enterprise_apply(p);
}
int main() {
    // Validation requested but the CA is missing, unreadable or too large: never connect.
    assert(!run(true, false, 0, 0) && !enabled && !ca_cleared);
    assert(!run(true, true, 10, 3) && !enabled && !ca_cleared);
    assert(!run(true, true, MAX_CA_PEM, MAX_CA_PEM) && !enabled && !ca_cleared);
    // A loaded CA is enforced.
    assert(run(true, true, 10, 10) && enabled && ca_set && !ca_cleared);
    // Validation off keeps the documented trust-on-connect behaviour.
    assert(run(false, false, 0, 0) && enabled && ca_cleared);
    return 0;
}
'''


class EnterpriseCaTests(unittest.TestCase):
    def test_requested_ca_validation_fails_closed(self):
        source = (ROOT / 'src/wifi_manager.cpp').read_text()
        start = source.index('static bool enterprise_apply(')
        end = source.index('// Start one association attempt')
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / 'enterprise_apply.inc').write_text(source[start:end])
            (pathlib.Path(tmp) / 'test.cpp').write_text(HARNESS)
            exe = str(pathlib.Path(tmp) / 'test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-I' + tmp, str(pathlib.Path(tmp) / 'test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
