"""Exercise production account scheduling/cache policy without network hardware."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MultiAccountTests(unittest.TestCase):
    def test_settings_add_remove_limit_and_stable_slots(self):
        subprocess.run(['node', 'tests/test_dexcom_ui.cjs'], cwd=ROOT, check=True)

    def test_credentials_limits_and_secret_retention(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'credentials'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                            '-I', str(ROOT / 'include'), str(ROOT / 'tests/test_dexcom_accounts.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_scheduler_cache_isolation_and_inflight_changes(self):
        source = (ROOT / 'src/http_client.cpp').read_text()
        # Only replace provider transports. Compile the real state, recording,
        # scheduling, publication, selection, freshness, history and alert code.
        state = source[source.index('struct AccountState'):source.index('// Helper: POST JSON')]
        policy = source[source.index('// Callers hold config_libre_mutex()'):]
        harness = r'''
#include "http_client.h"
#include "config_manager.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <cassert>
using std::min;
using std::max;
static unsigned long now_ms = 100;
unsigned long millis() { return now_ms; }
struct SerialStub { template<typename... T> void printf(const char*, T...) {} } Serial;
static AppConfig cfg = {};
AppConfig& config_get() { return cfg; }
std::recursive_mutex& config_libre_mutex() { static std::recursive_mutex m; return m; }
bool wifi_is_connected() { return true; }
bool config_has_server() { return true; }
void libre_reset_session() {}
__STATE__
static int calls[4] = {}, value[4] = {110, 80, 150, 200};
static bool fail[4] = {}, change_during_fetch = false, busy_test = false;
static bool dexcom_fetch_glucose(AccountState& s, const DexcomAccount& identity) {
    int i = identity.username[0] - '0';
    ++calls[i];
    if (busy_test) {
        busy_test = false;
        assert(!http_force_fetch()); // same transport cannot run concurrently
    }
    if (change_during_fetch) {
        change_during_fetch = false;
        http_reset_account(i);
    }
    if (fail[i]) { ++s.failure_count; return false; }
    s.current_reading.valid = true;
    s.current_reading.glucose = value[i];
    s.current_reading.timestamp = now_ms;
    s.current_reading.received_at_ms = now_ms;
    s.current_reading.force_mode = -1;
    s.last_success_ms = now_ms;
    s.ever_received = true;
    s.failure_count = 0;
    snprintf(s.dexcom_session_id, sizeof(s.dexcom_session_id), "session-%d", i);
    record_reading(s, value[i], now_ms);
    return true;
}
static bool libre_fetch_glucose(AccountState&) { return false; }
static void generic_fetch(AccountState&) {}
static void demo_generate(AccountState&) {}
__POLICY__
int main() {
    cfg.data_source = 1; cfg.poll_interval_sec = 60; cfg.stale_timeout_min = 20;
    strcpy(cfg.dexcom_username, "0"); strcpy(cfg.dexcom_password, "secret0");
    for (int i=1; i<4; ++i) {
        auto& a = cfg.extra_dexcom[i-1];
        snprintf(a.username, sizeof(a.username), "%d", i);
        snprintf(a.password, sizeof(a.password), "secret%d", i);
    }
    http_init();
    assert(http_account_count() == 4);
    for (int i=0; i<4; ++i) http_loop();
    for (int i=0; i<4; ++i) {
        assert(calls[i] == 1);
        http_select_account(i);
        assert(http_get_reading().glucose == value[i]);
        assert(http_get_delta() == 0);
        assert(accounts[i].dexcom_session_id[8] == '0' + i);
    }
    for (int i=0; i<20; ++i) http_loop();
    for (int i=0; i<4; ++i) assert(calls[i] == 1);
    now_ms += 60000;
    value[0] = 115; value[1] = 75; fail[2] = true;
    for (int i=0; i<4; ++i) http_loop();
    http_select_account(0); assert(http_get_delta() == 5);
    http_select_account(1); assert(http_get_delta() == -5);
    assert(http_find_alert(80, 250) == 1);
    GlucoseHistoryEntry history[48];
    assert(http_get_history(history, 48) == 2);
    assert(history[0].glucose == 80 && history[1].glucose == 75);
    http_select_account(2); assert(http_get_failure_count() == 1);
    assert(http_get_reading().glucose == 150);
    http_select_account(3); assert(http_get_failure_count() == 0);
    assert(calls[3] == 2); // failing account 2 did not starve account 3
    now_ms += 1200000;
    assert(http_find_alert(80, 250) == -1); // all readings stale
    http_select_account(0);
    change_during_fetch = true;
    assert(!http_force_fetch());
    assert(!http_get_reading().valid && !http_has_ever_received());
    assert(http_get_history(history, 48) == 0);
    assert(accounts[0].dexcom_session_id[0] == '\0');
    busy_test = true;
    assert(http_force_fetch());
    http_set_paused(true);
    const int before = calls[0];
    assert(!http_force_fetch()); assert(calls[0] == before);
    http_set_paused(false);
    http_select_account(2);
    cfg.extra_dexcom[1] = DexcomAccount{};
    http_reset_account(2);
    assert(http_selected_account() == 0 && http_account_count() == 3);
    assert(accounts[2].history_count == 0 && !accounts[2].ever_received);
    cfg.data_source = 3; assert(http_account_count() == 1);
}
'''.replace('__STATE__', state).replace('__POLICY__', policy)
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / 'accounts.cpp'
            cpp.write_text(harness)
            binary = Path(tmp) / 'accounts'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                            '-I', str(ROOT / 'include'), str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
