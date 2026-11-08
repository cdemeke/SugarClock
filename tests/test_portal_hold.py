import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
struct WifiTrialParams { char ssid[64]; };
enum WifiState { WIFI_ST_IDLE, WIFI_ST_CONNECTING, WIFI_ST_RETRY_WAIT, WIFI_ST_CONNECTED, WIFI_ST_SETUP_AP };
static unsigned long now_ms = 1000;
unsigned long millis() { return now_ms; }
struct { void println(const char*) {} template<class... A> void printf(const char*, A...) {} } Serial;
static int stations = 0, attempts = 0, disconnects = 0;
struct { int softAPgetStationNum() { return stations; } void disconnect(bool) { ++disconnects; } } WiFi;
static bool portal_up = true;
[[maybe_unused]] static void portal_start() { portal_up = true; }
static const char* status_str = "";
static WifiState state = WIFI_ST_RETRY_WAIT;
static unsigned long attempt_start_ms = 0, retry_wait_start_ms = 0;
[[maybe_unused]] static unsigned long first_failure_ms = 1;
static int last_disconnect_reason = 0;
static void params_from_config(WifiTrialParams& p) { strcpy(p.ssid, "home"); }
static void start_attempt(const WifiTrialParams&) { ++attempts; state = WIFI_ST_CONNECTING; attempt_start_ms = millis(); }
#define WIFI_CONNECT_TIMEOUT_MS 25000
#define WIFI_RETRY_INTERVAL_MS 30000
#define WIFI_SETUP_AP_AFTER_MS 120000
#include "portal_hold.inc"
static void run_for(unsigned long ms) { for (unsigned long end = now_ms + ms; now_ms < end; now_ms += 1000) loop_tail(); }
int main() {
    // A client on the portal pauses retries, but not forever.
    stations = 1;
    run_for(WIFI_PORTAL_HOLD_MAX_MS - 2000);
    assert(attempts == 0);
    run_for(4000);
    assert(attempts == 1);
    // The forced attempt is allowed to finish instead of being cut off.
    run_for(WIFI_CONNECT_TIMEOUT_MS - 2000);
    assert(state == WIFI_ST_CONNECTING && disconnects == 0);
    run_for(4000);
    assert(state == WIFI_ST_RETRY_WAIT);
    // Then a fresh pause begins for the client still connected.
    run_for(WIFI_PORTAL_HOLD_MAX_MS - 10000);
    assert(attempts == 1);
    run_for(20000);
    assert(attempts == 2);
    run_for(WIFI_CONNECT_TIMEOUT_MS + 2000);
    assert(state == WIFI_ST_RETRY_WAIT);
    // With nobody on the portal, ordinary retries resume on their interval.
    stations = 0; retry_wait_start_ms = now_ms; attempts = 0;
    run_for(WIFI_RETRY_INTERVAL_MS + 2000);
    assert(attempts == 1);
    // A client joining mid-attempt still stops an ordinary attempt immediately.
    stations = 1; loop_tail();
    assert(disconnects == 1 && state == WIFI_ST_RETRY_WAIT);
    return 0;
}
'''


class PortalHoldTests(unittest.TestCase):
    def test_setup_portal_client_cannot_pause_reconnects_forever(self):
        source = (ROOT / 'src/wifi_manager.cpp').read_text()
        define = source[source.index('#define WIFI_PORTAL_HOLD_MAX_MS'):].split('\n', 1)[0]
        statics = 'static unsigned long portal_hold_since_ms = 0;\nstatic bool portal_holding = false;\nstatic bool portal_forced_retry = false;\n'
        body = source[source.index('    // A phone on the portal pauses retries'):source.index('// ---------------------------------------------------------------------------\n// Accessors')]
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / 'portal_hold.inc').write_text(define + '\n' + statics + 'void loop_tail() {\n' + body)
            (pathlib.Path(tmp) / 'test.cpp').write_text(HARNESS)
            exe = str(pathlib.Path(tmp) / 'test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-Wno-unused-variable', '-I' + tmp, str(pathlib.Path(tmp) / 'test.cpp'), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
