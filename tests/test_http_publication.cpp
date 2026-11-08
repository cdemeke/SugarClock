#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include "http_client.h"
#include "network_schedule.h"
using std::max;
int published_mux=0;
void portENTER_CRITICAL(int*) {}
void portEXIT_CRITICAL(int*) {}
GlucoseReading current_reading{};
GlucoseHistoryEntry history_buf[GLUCOSE_HISTORY_SIZE]{};
int failure_count=0,last_response_code=200,current_delta=5,history_count=1,history_write_idx=1;
bool ever_received=true;
unsigned long last_success_ms=100,last_poll_ms=0,demo_last_update_ms=0;
uint32_t fetch_wifi_generation=1,dexcom_fallback_seconds=60;
std::atomic<bool> fetch_running{true},fetch_complete{false},http_paused{false},force_requested{false};
int polling_source=0;
scnet::DexcomSchedule dexcom_schedule;
unsigned long millis() {return 1000;}
struct AppConfig {bool glucose_enabled=true;int data_source=0,poll_interval_sec=60;} cfg;
AppConfig config_snapshot() {return cfg;}
bool wifi_is_connected() {return false;}
bool config_has_server() {return true;}
bool ble_acquire_network() {return false;}
void ble_release_network() {}
uint32_t wifi_connection_generation() {return 1;}
void fetch_worker(void*) {}
constexpr int pdPASS=1;
int xTaskCreate(void (*)(void*),const char*,int,void*,int,void*) {return 0;}
void demo_generate() {}
void http_init();
#include "http_publication.inc"
void http_init() {
    current_reading={};current_reading.force_mode=-1;
    memset(history_buf,0,sizeof(history_buf));history_count=history_write_idx=0;
    current_delta=0;ever_received=false;last_response_code=0;last_success_ms=0;
    publish_result();
}
int main() {
    current_reading.glucose=45;current_reading.valid=true;current_reading.force_mode=-1;
    history_buf[0].glucose=45;publish_result();
    assert(published.reading.valid && published.history_count==1);
    http_configuration_changed();
    assert(fetch_running && configuration_changed);
    assert(!published.reading.valid && !published.ever && published.history_count==0);
    assert(published.reading.force_mode==-1 && published.history[0].glucose==0);
    assert(published.code==0 && published.wifi_generation==0);
    http_loop();assert(fetch_running && configuration_changed);
    assert(!published.reading.valid); // The worker can remain blocked indefinitely.
    // An obsolete result cannot republish the old person's reading/history.
    current_reading.glucose=50;publish_result();assert(!published.reading.valid);
    fetch_running=false;fetch_complete=true;http_loop();
    assert(!configuration_changed && !published.reading.valid && fetch_generation==0);
    current_reading.glucose=110;current_reading.valid=true;ever_received=true;
    fetch_complete=true;http_loop();assert(published.reading.glucose==110 && fetch_generation==1);
}
