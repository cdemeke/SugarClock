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
int http_get_failure_count() {return 0;}
bool http_has_ever_received() {return true;}
unsigned long http_time_since_last_reading() {return 0;}
bool sysmon_has_data() {return false;}
bool time_is_available() {return true;}
bool wifi_is_ap_mode() {return false;}
bool wifi_is_connected() {return true;}
bool config_has_wifi() {return true;}
bool config_has_server() {return true;}
constexpr int NC_FAIL=2;
int netcheck_dns() {return 1;}int netcheck_data() {return 1;}
bool notify_has_active() {return false;}
unsigned beeps=0;
void buzzer_beep(int,int,int) {++beeps;}
#include "glucose_switch.inc"
int main() {
 cfg.glucose_enabled=true;cfg.alert_enabled=true;cfg.alert_low=70;cfg.alert_high=250;
 cfg.thresh_urgent_low=55;cfg.thresh_urgent_high=300;cfg.stale_timeout_min=20;
 cfg.time_display_enabled=true;cfg.ambient_enabled=true;
 engine_rebuild_toggle_order();assert(toggle_count==4);
 check_alerts();assert(beeps==1);assert(urgent_glucose_is_active());
 cfg.glucose_enabled=false;now+=2000;
 // Even an old valid urgent reading or a forced glucose view cannot override Off.
 check_alerts();assert(beeps==1);assert(!urgent_glucose_is_active());
 state_forced=true;reading.force_mode=STATE_GLUCOSE_DISPLAY;
 engine_rebuild_toggle_order();assert(toggle_count==2 && user_mode==STATE_TIME_DISPLAY);
 assert(evaluate_state()==STATE_TIME_DISPLAY);
 user_mode=STATE_AMBIENT_CREATURE_DISPLAY;assert(evaluate_state()==STATE_AMBIENT_CREATURE_DISPLAY);
 cfg.time_display_enabled=false;cfg.ambient_enabled=false;
 engine_rebuild_toggle_order();assert(toggle_count==1 && user_mode==STATE_TIME_DISPLAY);
 assert(evaluate_state()==STATE_TIME_DISPLAY);
 cfg.glucose_enabled=true;engine_rebuild_toggle_order();assert(toggle_count==2);
}
