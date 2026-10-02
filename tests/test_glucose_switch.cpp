#include "config_manager.h"
#include "glucose_engine.h"
#include <cassert>
#include <cstring>
#include <climits>
AppConfig cfg{};
AppConfig& config_get() {return cfg;}
unsigned long now=10000,boot_start_ms=0,last_cycle_ms=0,connection_info_expires_ms=0;
unsigned long alert_snooze_until_ms=0,last_beep_ms=0;
constexpr unsigned BEEP_INTERVAL_MS=1000;
constexpr int FAILURE_STALE_COUNT=3,FAILURE_NODATA_COUNT=5;
unsigned long millis() {return now;}
bool connection_info_visible=false,state_forced=false;
DisplayState forced_state=STATE_GLUCOSE_DISPLAY,user_mode=STATE_GLUCOSE_DISPLAY;
DisplayState toggle_order[12];int toggle_count=0,toggle_index=0;
char message_buf[64]{};
struct GlucoseReading {bool valid=true;int glucose=40,force_mode=-1;char message[64]{};} reading;
GlucoseReading http_get_reading() {return reading;}
int failures=0;
bool ever=true;
unsigned long readingAge=0;
int http_get_failure_count() {return failures;}
bool http_has_ever_received() {return ever;}
unsigned long http_time_since_last_reading() {return readingAge;}
bool weatherReady=false;
bool weather_has_data() {return weatherReady;}
bool sysmon_has_data() {return false;}
bool time_is_available() {return true;}
bool wifi_is_ap_mode() {return false;}
bool wifi_is_connected() {return true;}
bool config_has_wifi() {return true;}
bool config_has_server() {return true;}
constexpr int NC_FAIL=2;
int dnsResult=1,dataResult=1;
int netcheck_dns() {return dnsResult;}int netcheck_data() {return dataResult;}
bool notify_has_active() {return false;}
unsigned beeps=0;
void buzzer_beep(int,int,int) {++beeps;}
bool engine_low_glucose_lock_active() { return false; }
#include "glucose_switch.inc"
int main() {
 cfg.glucose_enabled=true;cfg.alert_enabled=true;cfg.alert_low=70;cfg.alert_high=250;
 cfg.thresh_urgent_low=55;cfg.thresh_urgent_high=300;cfg.stale_timeout_min=20;
 cfg.time_display_enabled=true;cfg.ambient_enabled=true;
 engine_rebuild_toggle_order();assert(toggle_count==4);
 // Enabled-but-empty weather must not occupy a manual or auto-cycle slot.
 cfg.weather_enabled=true;user_mode=STATE_WEATHER_DISPLAY;
 engine_rebuild_toggle_order();assert(toggle_count==4 && user_mode==STATE_GLUCOSE_DISPLAY);
 for(int i=0;i<toggle_count;++i) assert(toggle_order[i]!=STATE_WEATHER_DISPLAY);
 // First successful fetch adds weather; loss of data removes a selected weather
 // screen without disturbing glucose/time/pet availability.
 weatherReady=true;engine_rebuild_toggle_order();assert(toggle_count==5);
 assert(toggle_order[3]==STATE_WEATHER_DISPLAY);
 user_mode=STATE_WEATHER_DISPLAY;weatherReady=false;
 engine_rebuild_toggle_order();assert(toggle_count==4 && user_mode==STATE_GLUCOSE_DISPLAY);
 weatherReady=true;cfg.weather_enabled=false;
 engine_rebuild_toggle_order();assert(toggle_count==4);
 weatherReady=false;
 check_alerts();assert(beeps==1);assert(urgent_glucose_is_active());
 // A failed earlier reachability probe cannot obscure recovered fresh glucose,
 // including urgent low readings. Actual missing/stale data retains diagnostics.
 dataResult=NC_FAIL;
 assert(evaluate_state()==STATE_GLUCOSE_DISPLAY);
 now+=2000;check_alerts();assert(beeps==2);
 dnsResult=NC_FAIL;assert(evaluate_state()==STATE_GLUCOSE_DISPLAY);
 readingAge=20UL*60*1000;assert(evaluate_state()==STATE_NET_LIMITED);
 readingAge=0;failures=FAILURE_STALE_COUNT;assert(evaluate_state()==STATE_NET_LIMITED);
 failures=0;reading.valid=false;assert(evaluate_state()==STATE_NET_LIMITED);
 reading.valid=true;ever=false;assert(evaluate_state()==STATE_NET_LIMITED);
 ever=true;dnsResult=dataResult=1;
 cfg.glucose_enabled=false;now+=2000;
 // Even an old valid urgent reading or a forced glucose view cannot override Off.
 check_alerts();assert(beeps==2);assert(!urgent_glucose_is_active());
 state_forced=true;reading.force_mode=STATE_GLUCOSE_DISPLAY;
 engine_rebuild_toggle_order();assert(toggle_count==2 && user_mode==STATE_TIME_DISPLAY);
 assert(evaluate_state()==STATE_TIME_DISPLAY);
 user_mode=STATE_AMBIENT_CREATURE_DISPLAY;assert(evaluate_state()==STATE_AMBIENT_CREATURE_DISPLAY);
 cfg.time_display_enabled=false;cfg.ambient_enabled=false;
 engine_rebuild_toggle_order();assert(toggle_count==1 && user_mode==STATE_TIME_DISPLAY);
 assert(evaluate_state()==STATE_TIME_DISPLAY);
 cfg.glucose_enabled=true;engine_rebuild_toggle_order();assert(toggle_count==2);
}
