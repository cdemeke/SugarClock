#include <ArduinoJson.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "wifi_manager.h"
#include "config_manager.h"
#include "config_patch.h"

// Check memory immediately before free (never read freed storage). This also
// observes JSON growth/shrink allocations and aborted request bodies.
static std::unordered_map<void*,size_t> allocations;
static bool failAllocation=false;
static size_t wipedAllocations=0;
static void* observed_malloc(size_t size) {
    if (failAllocation) {failAllocation=false;return nullptr;}
    void* p=std::malloc(size);
    if(p) allocations[p]=size;
    return p;
}
static void* observed_calloc(size_t count,size_t size) {
    void* p=observed_malloc(count*size);
    if(p) std::memset(p,0,count*size);
    return p;
}
static void observed_free(void* p) {
    if(!p) return;
    auto it=allocations.find(p);assert(it!=allocations.end());
    for(size_t i=0;i<it->second;++i) assert(static_cast<unsigned char*>(p)[i]==0);
    ++wipedAllocations;allocations.erase(it);std::free(p);
}
#define malloc observed_malloc
#define calloc observed_calloc
#define free observed_free
#include "web_request_body.h"
#include "sensitive_json.h"
#undef malloc
#undef calloc
#undef free

using String=std::string;
struct AsyncWebServerRequest {
    void* _tempObject=nullptr;
    int code=0,sends=0;
    std::function<void()> disconnect;
    void* getResponse() {return code ? this : nullptr;}
    void send(int status,const char*,const String&) {code=status;++sends;}
    void onDisconnect(std::function<void()> fn) {disconnect=fn;}
    ~AsyncWebServerRequest() {
        // Pinned library calls onDisconnect before freeing _tempObject.
        if(disconnect) disconnect();
        assert(_tempObject==nullptr);
    }
};
static std::vector<WifiTrialParams> trials;
static std::vector<String> certificates;
static bool trialAllowed=true,certificateAllowed=true;
bool wifi_trial_start(const WifiTrialParams& p) {
    if(!trialAllowed) return false;
    trials.push_back(p);return true;
}
bool config_ca_exists() {return true;}
bool config_ca_write(const char* data,size_t len) {
    if(!certificateAllowed) return false;
    certificates.emplace_back(data,len);return true;
}
static AppConfig current={};
static int configSaves=0;
void config_lock() {}
void config_unlock() {}
AppConfig& config_get() {return current;}
bool ota_is_busy() {return false;}
bool settings_apply(const AppConfig& candidate) {current=candidate;++configSaves;return true;}

// The production handlers, extracted without rewriting their logic.
#include "web_bodies.inc"
using Handler=void(*)(AsyncWebServerRequest*,uint8_t*,size_t,size_t,size_t);
static void chunk(Handler handler,AsyncWebServerRequest& r,const String& body,size_t index,size_t len) {
    handler(&r,reinterpret_cast<uint8_t*>(const_cast<char*>(body.data()+index)),len,index,body.size());
}
static void malformed(Handler handler,size_t maximum) {
    uint8_t payload[4000]={};
    for(auto params:std::vector<std::vector<size_t>>{
        {4000,0,10}, {1,11,10}, {SIZE_MAX,1,10}, {1,SIZE_MAX,10},
        {1,0,maximum+1}, {1,0,SIZE_MAX}}) {
        AsyncWebServerRequest r;
        handler(&r,payload,params[0],params[1],params[2]);
        assert(r.code==413 && r._tempObject==nullptr);
        // A rejected request cannot restart on a later callback.
        handler(&r,payload,1,0,1);assert(r.sends==1);
    }
    for(int scenario=0;scenario<5;++scenario) {
        AsyncWebServerRequest r;
        if(scenario==0) handler(&r,payload,1,1,4); // missing first chunk
        else {
            handler(&r,payload,1,0,4);assert(r.code==0 && r._tempObject);
            if(scenario==1) handler(&r,payload,1,0,4); // duplicate
            if(scenario==2) handler(&r,payload,1,2,4); // gap
            if(scenario==3) handler(&r,payload,1,1,5); // total changed
            if(scenario==4) handler(&r,payload,4,1,4); // overrun after partial body
        }
        assert(r.code==(scenario==4?413:400));assert(!r._tempObject);
    }
    {
        AsyncWebServerRequest r;failAllocation=true;
        handler(&r,payload,1,0,4);assert(r.code==503 && !r._tempObject);
    }
    {
        AsyncWebServerRequest r;
        handler(&r,payload,1,0,4);assert(r._tempObject); // abort with partial body
    }
    {
        AsyncWebServerRequest r;
        handler(&r,payload,0,0,0);assert(r.code==400);
    }
    assert(allocations.empty());
}
int main() {
    current.thresh_urgent_low=55;current.thresh_low=70;current.thresh_high=180;current.thresh_urgent_high=250;
    current.alert_low=70;current.alert_high=180;
    malformed(handle_wifi_connect,1023);
    malformed(handle_wifi_ca_upload,4095);
    malformed(handle_post_config,4095);
    assert(trials.empty() && certificates.empty() && configSaves==0);

    const String a=R"({"ssid":"Office A","security":1,"identity":"alice","password":"secret-A"})";
    const String b=R"({"ssid":"Office B","security":1,"identity":"bob","password":"secret-B"})";
    {
        AsyncWebServerRequest first,second;
        chunk(handle_wifi_connect,first,a,0,20);
        chunk(handle_wifi_connect,second,b,0,35);
        chunk(handle_wifi_connect,first,a,20,a.size()-20);
        chunk(handle_wifi_connect,second,b,35,b.size()-35);
        assert(first.code==202 && second.code==202 && trials.size()==2);
        assert(String(trials[0].ssid)=="Office A" && String(trials[0].identity)=="alice" && String(trials[0].password)=="secret-A");
        assert(String(trials[1].ssid)=="Office B" && String(trials[1].identity)=="bob" && String(trials[1].password)=="secret-B");
        chunk(handle_wifi_connect,first,a,0,a.size());assert(trials.size()==2 && first.sends==1);
        assert(allocations.empty());
    }
    const String pemA="-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----";
    const String pemB="-----BEGIN CERTIFICATE-----\nBBBB\n-----END CERTIFICATE-----";
    {
        AsyncWebServerRequest first,second;
        chunk(handle_wifi_ca_upload,first,pemA,0,30);
        chunk(handle_wifi_ca_upload,second,pemB,0,30);
        chunk(handle_wifi_ca_upload,second,pemB,30,pemB.size()-30);
        chunk(handle_wifi_ca_upload,first,pemA,30,pemA.size()-30);
        assert(first.code==200 && second.code==200);
        assert(certificates.size()==2 && certificates[0]==pemB && certificates[1]==pemA);
    }
    // Boundary-sized valid bodies and byte-at-a-time fragmentation.
    for(auto entry:std::vector<std::pair<Handler,String>>{
        {handle_wifi_connect,a+String(1023-a.size(),' ')},
        {handle_wifi_ca_upload,pemA+String(4095-pemA.size(),' ')},
        {handle_post_config,R"({"brightness":40,"dexcom_password":"temporary-secret"})"}}) {
        AsyncWebServerRequest r;
        for(size_t i=0;i<entry.second.size();++i) chunk(entry.first,r,entry.second,i,1);
        assert(r.code==200 || r.code==202);assert(allocations.empty());
    }
    assert(configSaves==1 && String(current.dexcom_password)=="temporary-secret");
    // Parse, validation, queue rejection and storage failure also erase inputs.
    for(const String& invalid:std::vector<String>{"{bad",R"({"password":"secret"})",R"({"ssid":"x","security":9,"password":"secret"})"}) {
        AsyncWebServerRequest r;chunk(handle_wifi_connect,r,invalid,0,invalid.size());
        assert(r.code==400 && allocations.empty());
    }
    {
        AsyncWebServerRequest r;trialAllowed=false;chunk(handle_wifi_connect,r,a,0,a.size());assert(r.code==409);
    }
    {
        AsyncWebServerRequest r;certificateAllowed=false;chunk(handle_wifi_ca_upload,r,pemA,0,pemA.size());assert(r.code==500);
    }
    {
        AsyncWebServerRequest r;String bad="not a certificate";
        chunk(handle_wifi_ca_upload,r,bad,0,bad.size());assert(r.code==400);
    }
    {
        SensitiveJsonAllocator allocator;
        void* p=allocator.allocate(8);std::memset(p,0x55,8);
        failAllocation=true;assert(!allocator.reallocate(p,16));
        assert(static_cast<uint8_t*>(p)[0]==0x55);allocator.deallocate(p);
    }
    char secret[]="sensitive";
    {SensitiveScope<decltype(secret)> erase(secret);}
    for(char c:secret) assert(c==0);
    assert(allocations.empty() && wipedAllocations>20);
}
