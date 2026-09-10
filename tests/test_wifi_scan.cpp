#include <cassert>
#include <cstdint>
#include <ArduinoJson.h>
#include "wifi_manager.h"
constexpr int WIFI_SCAN_FAILED=-2,WIFI_SCAN_RUNNING=-1,WIFI_AP_STA=3,WIFI_MODE_NULL=0,WIFI_STA=1;
struct Driver {
 int result=WIFI_SCAN_RUNNING,calls=0;
 int getMode() {return WIFI_STA;}
 void mode(int) {}
 int scanNetworks(bool,bool) {++calls;return result;}
 int scanComplete() {return result;}
} WiFi;
struct {void println(const char*) {}} Serial;
bool scan_running=false,scan_failed=false,portal_up=false,boot_connect_pending=false,otaBusy=false;
int collects=0;
WifiScanEntry cache={"Old network",-50,1,3,false};
bool config_has_wifi() {return false;}
void params_from_config(WifiTrialParams&) {}
void start_attempt(WifiTrialParams&) {}
void scan_collect() {++collects;scan_running=false;}
bool wifi_scan_in_progress() {return scan_running;}
int wifi_scan_count() {return 1;}
const WifiScanEntry* wifi_scan_get(int) {return &cache;}
bool ota_is_busy() {return otaBusy;}
#include "wifi_scan.inc"
#include "ble_scan.inc"
int main() {
 JsonDocument out;
 WiFi.result=WIFI_SCAN_FAILED;
 execute_scan("wifi.scan",out);
 assert(out["error"]=="scan_failed" && !scan_running && wifi_scan_failed());
 out.clear();execute_scan("wifi.results",out);
 assert(out["error"]=="scan_failed" && out["networks"].isNull());
 WiFi.result=WIFI_SCAN_RUNNING;out.clear();execute_scan("wifi.scan",out);
 assert(out["state"]=="queued" && scan_running && !wifi_scan_failed());
 int calls=WiFi.calls;out.clear();execute_scan("wifi.scan",out);
 assert(WiFi.calls==calls && out["state"]=="queued");
 WiFi.result=WIFI_SCAN_FAILED;poll_scan();
 out.clear();execute_scan("wifi.results",out);
 assert(!scan_running && wifi_scan_failed() && out["error"]=="scan_failed");
 WiFi.result=WIFI_SCAN_RUNNING;out.clear();execute_scan("wifi.scan",out);
 WiFi.result=1;poll_scan();out.clear();execute_scan("wifi.results",out);
 assert(collects==1 && !wifi_scan_failed());
 assert(out["scanning"]==false && out["networks"][0]["ssid"]=="Old network");
 otaBusy=true;out.clear();execute_scan("wifi.scan",out);assert(out["error"]=="busy");
}
