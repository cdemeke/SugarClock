#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "config_manager.h"
#include "ota_manager.h"
using String=std::string;
struct AsyncWebServerRequest {
    int code=0;String body;
    void send(int status,const char*,const String& output) {code=status;body=output;}
};
AppConfig config{};
int configDepth=0;
bool durable=false,pending=true,failed=false,completeDuringSnapshot=false;
std::vector<int> order;
void config_lock() {++configDepth;}
void config_unlock() {--configDepth;}
AppConfig& config_get() {assert(configDepth>0);return config;}
bool config_is_durable() {assert(configDepth>0);return durable;}
bool config_ca_exists() {return false;}
void add_libre_people(JsonDocument&) {}
String color_to_hex(uint32_t color) {char value[8];snprintf(value,sizeof(value),"#%06x",unsigned(color));return value;}
bool fleet_request_pending() {order.push_back(1);return pending;}
bool fleet_request_failed() {order.push_back(3);return failed;}
void ota_get_status(OtaStatusSnapshot& snapshot) {
    order.push_back(2);snapshot={};snapshot.state=OTA_IDLE;
    strcpy(snapshot.current_version,"0.3.6");
    if(completeDuringSnapshot) pending=false;
}
const char* ota_state_name(OtaState) {return "idle";}
#include "http_evidence.inc"
int main() {
    config.brightness=120;
    for(bool saved:{false,true}) {
        config.night_disable_auto_cycle=saved;
        durable=saved;AsyncWebServerRequest request;handle_get_config(&request);
        assert(request.code==200 && configDepth==0);
        JsonDocument doc;assert(!deserializeJson(doc,request.body));
        assert(doc["configuration_saved"].is<bool>());
        assert(doc["configuration_saved"].as<bool>()==saved);
        assert(doc["brightness"].as<int>()==120);
        assert(doc["night_disable_auto_cycle"].is<bool>() && doc["night_disable_auto_cycle"].as<bool>()==saved);
    }
    for(bool queued:{false,true}) for(bool failure:{false,true}) {
        pending=queued;failed=failure;order.clear();AsyncWebServerRequest request;
        handle_ota_status(&request);assert(request.code==200);
        JsonDocument doc;assert(!deserializeJson(doc,request.body));
        assert(doc["request_pending"].is<bool>() && doc["request_failed"].is<bool>());
        assert(doc["request_pending"].as<bool>()==queued);
        assert(doc["request_failed"].as<bool>()==(!queued && failure));
        assert(order[0]==1 && order[1]==2);
    }
    // A worker finishing during the snapshot cannot attach "finished" to an
    // earlier idle state: this poll remains pending and the next one is fresh.
    pending=true;failed=false;completeDuringSnapshot=true;order.clear();
    AsyncWebServerRequest request;handle_ota_status(&request);
    JsonDocument doc;assert(!deserializeJson(doc,request.body));
    assert(doc["request_pending"].as<bool>() && !pending);
}
