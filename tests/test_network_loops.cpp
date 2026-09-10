#include <atomic>
#include <cassert>
#include <algorithm>
#include "network_schedule.h"
#include "fleet_policy.h"
using std::max;
uint32_t now=100000;
uint32_t millis() {return now;}
struct Config {int data_source=1,poll_interval_sec=60;bool glucose_enabled=true;} config;
using AppConfig=Config;
uint32_t dexcom_fallback_seconds=60;
int polling_source=0;
bool glucoseExpected=false;
bool http_dexcom_due_within(uint32_t) {return glucoseExpected;}
Config config_snapshot() {return config;}
bool online=true,server=true,pausedWindow=false,trustedTime=true,otaBusy=false;
bool lease=false,failTask=false;
bool wifi_is_connected() {return online;}
bool config_has_server() {return server;}
bool wifi_is_ap_mode() {return false;}
bool time_is_available() {return trustedTime;}
bool ota_is_busy() {return otaBusy;}
std::atomic<bool> fetch_running{false},fetch_complete{false},force_requested{false};
std::atomic<bool> configuration_changed{false},http_paused{false},worker_running{false};
bool http_is_fetching() {return fetch_running;}
void http_set_paused(bool value) {http_paused=value;}
bool weatherPaused=false;
void weather_set_paused(bool value) {weatherPaused=value;}
unsigned fetch_generation=0,publications=0;
void publish_result() {++publications;}
unsigned long last_poll_ms=0,demo_last_update_ms=0;
void demo_generate() {demo_last_update_ms=now;}
scnet::DexcomSchedule dexcom_schedule;
void http_init() {dexcom_schedule={};last_poll_ms=0;}
int last_response_code=0;
unsigned failure_count=0;
uint32_t next_attempt_ms=0;
unsigned acquisitions=0,glucoseTasks=0,fleetTasks=0;
bool ble_acquire_network() {if(lease)return false;lease=true;++acquisitions;return true;}
void ble_release_network() {lease=false;}
bool ble_network_batch_window() {return pausedWindow && !lease;}
void fetch_worker(void*) {}
void fleet_worker(void*) {}
constexpr int pdPASS=1;
int xTaskCreate(void (*worker)(void*),const char*,int,void*,int,void*) {
 if(failTask) return 0;
 if(worker==fetch_worker) ++glucoseTasks;else ++fleetTasks;
 return pdPASS;
}
struct {template<class... T> void printf(const char*,T...) {} void println(const char*) {}} Serial;
#include "http_loop.inc"
#include "fleet_loop.inc"
int main() {
 dexcom_schedule.complete(now,2000000000,2000000000,true,60);
 now+=60000;http_loop();assert(glucoseTasks==0); // Ignore fixed 60s on Dexcom.
 force_requested=true;online=false;http_loop();assert(force_requested && !fetch_running);
 online=true;lease=true;http_loop();assert(force_requested && !fetch_running);
 lease=false;http_loop();assert(glucoseTasks==1 && fetch_running && !force_requested);
 // An active glucose task prevents the management scheduler from overlapping it.
 pausedWindow=true;next_attempt_ms=now;fleet_loop();assert(fleetTasks==0);
 fetch_running=false;fetch_complete=true;lease=false;http_paused=true;
 http_loop();assert(publications==1 && fetch_generation==1);
 http_paused=false;
 // Task creation failure releases the lease and schedules a paced retry.
 force_requested=true;failTask=true;http_loop();
 assert(!lease && !fetch_running && last_response_code==-1000);
 assert(!dexcom_schedule.ready(now+59999) && dexcom_schedule.ready(now+60000));
 failTask=false;failure_count=0;
 configuration_changed=true;http_loop();assert(glucoseTasks==2); // Fresh config starts promptly.
 fetch_running=false;lease=false;
 config.data_source=0;last_poll_ms=now;now+=59999;http_loop();assert(glucoseTasks==2);
 ++now;http_loop();assert(glucoseTasks==3); // Custom HTTP keeps configured cadence.
 fetch_running=false;lease=false;
 // Group a successful management check-in with the existing pause, even early.
 next_attempt_ms=now+30000;fleet_loop();assert(fleetTasks==1 && worker_running && lease);
 assert(http_paused && weatherPaused);
 worker_running=false;lease=false;http_paused=false;weatherPaused=false;
 failure_count=1;fleet_loop();assert(fleetTasks==1); // Do not shorten retry backoff.
 now=next_attempt_ms;fleet_loop();assert(fleetTasks==2);
 worker_running=false;lease=false;pausedWindow=false;failure_count=0;
 next_attempt_ms=now;fleet_loop();assert(fleetTasks==2);
 glucoseExpected=true;now+=60000;fleet_loop();assert(fleetTasks==2);
 glucoseExpected=false;
 now+=60000;trustedTime=false;fleet_loop();assert(fleetTasks==2);
 trustedTime=true;otaBusy=true;fleet_loop();assert(fleetTasks==2);
 otaBusy=false;failTask=true;fleet_loop();
 assert(!worker_running && !lease && !http_paused && !weatherPaused);
 pausedWindow=true;unsigned before=acquisitions;fleet_loop();assert(acquisitions==before);
 failTask=false;now=next_attempt_ms+60000;fleet_loop();assert(fleetTasks==3);
 // Disabled sources neither generate demo data nor acquire a network lease.
 worker_running=false;lease=false;http_paused=false;fetch_running=false;
 config.glucose_enabled=false;
 unsigned tasksBefore=glucoseTasks,leasesBefore=acquisitions,readsBefore=publications;
 for(int source=0;source<3;++source) {
  config.data_source=source;force_requested=true;now+=3600000;http_loop();
  assert(!force_requested && !fetch_running && !lease);
  assert(glucoseTasks==tasksBefore && acquisitions==leasesBefore && publications==readsBefore);
 }
 fetch_complete=true;configuration_changed=true;http_loop();assert(publications==readsBefore);
 fetch_complete=true;http_loop();assert(publications==readsBefore);
 config.glucose_enabled=true;config.data_source=1;configuration_changed=true;
 http_loop();assert(glucoseTasks==tasksBefore+1 && fetch_running);

}
