#include "config_manager.h"
#include "glucose_engine.h"
#include "buzzer.h"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <climits>
AppConfig cfg{};
AppConfig& config_get() {return cfg;}
unsigned long now=10000,boot_start_ms=0,last_cycle_ms=0,connection_info_expires_ms=0;
bool alert_snoozed=false;unsigned long alert_snooze_start_ms=0;
constexpr int FAILURE_STALE_COUNT=3,FAILURE_NODATA_COUNT=5;
unsigned long millis() {return now;}
bool connection_info_visible=false,state_forced=false;
DisplayState forced_state=STATE_GLUCOSE_DISPLAY,default_mode=STATE_GLUCOSE_DISPLAY,user_mode=STATE_GLUCOSE_DISPLAY;
bool weather_startup_pending=false;
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
AlarmKind alarm=ALARM_NONE;
uint16_t alarm_hz=0;AlarmStyle alarm_style=ALARM_STYLE_COUNT;
void buzzer_set_alarm(AlarmKind kind,uint16_t hz,AlarmStyle style) {alarm=kind;alarm_hz=hz;alarm_style=style;}
AlarmKind buzzer_active_alarm() {return alarm;}
bool buzzer_alarm_output_on() {return false;}
bool buzzer_test_active() {return false;}
uint16_t buzzer_sweep_hz() {return 0;}
void notify_push(const char*,int,bool) {}
void display_set_brightness(uint8_t) {}
void display_show() {}
uint8_t effective_brightness() {return 40;}
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
 cfg.alarm_hz=2700;cfg.alarm_style=ALARM_STYLE_WARBLE;
 check_alerts();assert(alarm==ALARM_URGENT && alarm_hz==2700 && alarm_style==ALARM_STYLE_WARBLE);assert(urgent_glucose_is_active());update_alarm_flash();
 // Out-of-range saved values fall back to the defaults.
 cfg.alarm_hz=50;cfg.alarm_style=9;check_alerts();assert(alarm_hz==ALARM_DEFAULT_HZ && alarm_style==ALARM_STYLE_STEADY);
 // A failed earlier reachability probe cannot obscure recovered fresh glucose,
 // including urgent low readings. Actual missing/stale data retains diagnostics.
 dataResult=NC_FAIL;
 assert(evaluate_state()==STATE_GLUCOSE_DISPLAY);
 now+=2000;alarm=ALARM_NONE;check_alerts();assert(alarm==ALARM_URGENT);
 dnsResult=NC_FAIL;assert(evaluate_state()==STATE_GLUCOSE_DISPLAY);
 readingAge=20UL*60*1000;assert(evaluate_state()==STATE_NET_LIMITED);
 readingAge=0;failures=FAILURE_STALE_COUNT;assert(evaluate_state()==STATE_NET_LIMITED);
 failures=0;reading.valid=false;assert(evaluate_state()==STATE_NET_LIMITED);
 reading.valid=true;ever=false;assert(evaluate_state()==STATE_NET_LIMITED);
 ever=true;dnsResult=dataResult=1;
 cfg.glucose_enabled=false;now+=2000;
 // Even an old valid urgent reading or a forced glucose view cannot override Off.
 check_alerts();assert(alarm==ALARM_NONE);assert(!urgent_glucose_is_active());
 state_forced=true;reading.force_mode=STATE_GLUCOSE_DISPLAY;
 engine_rebuild_toggle_order();assert(toggle_count==2 && user_mode==STATE_TIME_DISPLAY);
 assert(evaluate_state()==STATE_TIME_DISPLAY);
 user_mode=STATE_AMBIENT_CREATURE_DISPLAY;assert(evaluate_state()==STATE_AMBIENT_CREATURE_DISPLAY);
 cfg.time_display_enabled=false;cfg.ambient_enabled=false;
 engine_rebuild_toggle_order();assert(toggle_count==1 && user_mode==STATE_TIME_DISPLAY);
 assert(evaluate_state()==STATE_TIME_DISPLAY);
 cfg.glucose_enabled=true;engine_rebuild_toggle_order();assert(toggle_count==2);
}
