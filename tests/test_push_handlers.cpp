#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <string>
#include "config_manager.h"
#include "web_request_body.h"

using String=std::string;
struct AsyncWebServerRequest {
    void* _tempObject=nullptr;
    int code=0;
    std::function<void()> disconnect;
    void* getResponse() {return code ? this : nullptr;}
    void send(int status,const char*,const String&) {code=status;}
    void onDisconnect(std::function<void()> fn) {disconnect=fn;}
    ~AsyncWebServerRequest() {if(disconnect) disconnect();}
};
template<class T> static T constrain(T v,T lo,T hi) {return std::min(std::max(v,lo),hi);}
static AppConfig current={};
AppConfig& config_get() {return current;}
void config_lock() {}
void config_unlock() {}
static int pushes=0,lastDuration=0,lastValue=0,lastMax=0;
static std::string lastText;
void notify_push(const char* text,int duration,bool) {++pushes;lastText=text;lastDuration=duration;}
void sysmon_push(const char*,int value,int max) {++pushes;lastValue=value;lastMax=max;}
#include "push_handlers.inc"

using Handler=void(*)(AsyncWebServerRequest*,uint8_t*,size_t,size_t,size_t);
static int post(Handler h,const char* json,size_t split=0) {
    AsyncWebServerRequest r;std::string body=json;size_t n=body.size();
    if(split && split<n) {
        h(&r,(uint8_t*)body.data(),split,0,n);
        h(&r,(uint8_t*)body.data()+split,n-split,split,n);
    } else h(&r,(uint8_t*)body.data(),n,0,n);
    return r.code;
}

int main() {
    current.notify_enabled=true;current.sysmon_enabled=true;current.notify_default_duration=60;
    strcpy(current.sysmon_label,"CPU");
    // A body split across chunks is parsed whole, not from its first chunk.
    const char* split="{\"text\":\"hello world\",\"duration_sec\":30}";
    assert(post(handle_post_notify,split,10)==200 && lastText=="hello world" && lastDuration==30);
    // Out-of-range or non-integer durations are rejected, not wrapped.
    pushes=0;
    for(const char* bad:{"{\"text\":\"x\",\"duration_sec\":-1}","{\"text\":\"x\",\"duration_sec\":0}",
                         "{\"text\":\"x\",\"duration_sec\":2147483647}","{\"text\":\"x\",\"duration_sec\":\"5\"}"})
        assert(post(handle_post_notify,bad)==400);
    assert(pushes==0);
    assert(post(handle_post_notify,"{\"text\":\"x\"}")==200 && lastDuration==60);
    // A Content-Length that does not match the data is refused.
    {AsyncWebServerRequest r;std::string b="{\"text\":\"x\"}";handle_post_notify(&r,(uint8_t*)b.data(),b.size(),0,1);assert(r.code==413);}
    // System monitor: max must be sane and value is clamped to it.
    assert(post(handle_post_sysmon,"{\"value\":2147483647,\"max\":1}")==200 && lastValue==1 && lastMax==1);
    assert(post(handle_post_sysmon,"{\"value\":-5,\"max\":100}")==200 && lastValue==0);
    assert(post(handle_post_sysmon,"{\"value\":42,\"max\":100}",8)==200 && lastValue==42);
    pushes=0;
    for(const char* bad:{"{\"value\":1,\"max\":0}","{\"value\":1,\"max\":-3}","{\"value\":1,\"max\":2147483647}"})
        assert(post(handle_post_sysmon,bad)==400);
    assert(pushes==0);
    return 0;
}
