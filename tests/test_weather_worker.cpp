#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "weather_client.h"
using std::max;
using portMUX_TYPE=int;
constexpr int portMUX_INITIALIZER_UNLOCKED=0,pdPASS=1;
void portENTER_CRITICAL(int*) {}
void portEXIT_CRITICAL(int*) {}
struct AppConfig {bool weather_enabled=false;char weather_api_key[48]="test";char weather_city[64]="London,GB";int weather_poll_min=5;} cfg;
AppConfig config_snapshot() {return cfg;}
unsigned long now=1000;
unsigned long millis() {return now;}
bool online=true,lease=false,taskFails=false,fetchOK=true;
unsigned acquisitions=0,releases=0,fetches=0,tasks=0,redraws=0;
void (*pendingWorker)(void*)=nullptr;
bool wifi_is_connected() {return online;}
bool ble_acquire_network() {if(lease) return false;lease=true;++acquisitions;return true;}
void ble_release_network() {assert(lease);lease=false;++releases;}
void vTaskDelete(void*) {}
int xTaskCreate(void (*worker)(void*),const char*,int,void*,int,void*) {
    if(taskFails) return 0;
    ++tasks;pendingWorker=worker;return pdPASS;
}
bool weather_do_fetch(const AppConfig&,WeatherSnapshot& result) {
    assert(lease);++fetches;result.http_code=fetchOK ? 200:503;
    if(fetchOK) {result.reading.temp=70;result.reading.valid=true;result.ever_received=true;}
    return fetchOK;
}
void redraw() {++redraws;}
#include "weather_worker.inc"

int main() {
    weather_init();pre_fetch_cb=redraw;
    weather_set_paused(true);assert(!weather_force_fetch());
    weather_set_paused(false);assert(weather_force_fetch());
    // A competing HTTPS owner leaves the test queued without touching it.
    lease=true;weather_loop();assert(tasks==0 && weather_force_pending());
    lease=false;weather_loop();
    assert(tasks==1 && fetches==0 && fetch_running && weather_force_pending());
    assert(lease && redraws==1 && weather_force_generation()==0);
    // A blocked worker never runs inline: main-loop work keeps progressing.
    for(int buttonTicks=0;buttonTicks<100;++buttonTicks) weather_loop();
    assert(tasks==1 && fetches==0 && !weather_has_data());
    assert(!weather_force_fetch());
    pendingWorker(nullptr);
    assert(!lease && releases==1 && weather_force_pending());
    assert(!weather_has_data()); // Incomplete publication is never visible.
    weather_set_paused(true);weather_loop();
    assert(weather_has_data() && weather_get_reading().temp==70);
    assert(weather_last_force_ok() && weather_force_generation()==1 && !weather_force_pending());
    weather_set_paused(false);taskFails=true;assert(weather_force_fetch());weather_loop();
    assert(!fetch_running && !lease && !weather_force_pending());
    assert(!weather_last_force_ok() && weather_force_generation()==2);
    assert(weather_snapshot().http_code==-1000);
    assert(weather_has_data()); // Failure keeps the last valid reading.
    taskFails=false;fetchOK=false;assert(weather_force_fetch());weather_loop();
    pendingWorker(nullptr);weather_loop();
    assert(weather_force_generation()==3 && !weather_last_force_ok());
    assert(weather_snapshot().http_code==503 && releases==acquisitions);
    // Regular polling uses the same worker and lease, even with no queued test.
    cfg.weather_enabled=true;now+=300001;weather_loop();
    assert(fetch_running && !weather_force_pending() && lease);
    pendingWorker(nullptr);weather_loop();
    assert(!lease && weather_force_generation()==3);
}
