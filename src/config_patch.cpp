#include "config_patch.h"
#include <string.h>
#include <stdlib.h>
#include <string>
namespace {
enum class Kind { Text,Int,Bool,Byte,UInt,ULong };
struct Field { const char* name; size_t offset,size; Kind kind; long lo,hi; bool secret; };
const Field fields[] = {
    {"wifi_ssid",offsetof(AppConfig,wifi_ssid),sizeof(((AppConfig*)0)->wifi_ssid),Kind::Text,0,2147483647,false},
    {"wifi_password",offsetof(AppConfig,wifi_password),sizeof(((AppConfig*)0)->wifi_password),Kind::Text,0,2147483647,true},
    {"wifi_security",offsetof(AppConfig,wifi_security),sizeof(((AppConfig*)0)->wifi_security),Kind::Int,0,1,false},
    {"wifi_eap_method",offsetof(AppConfig,wifi_eap_method),sizeof(((AppConfig*)0)->wifi_eap_method),Kind::Int,0,1,false},
    {"wifi_identity",offsetof(AppConfig,wifi_identity),sizeof(((AppConfig*)0)->wifi_identity),Kind::Text,0,2147483647,false},
    {"wifi_eap_password",offsetof(AppConfig,wifi_eap_password),sizeof(((AppConfig*)0)->wifi_eap_password),Kind::Text,0,2147483647,true},
    {"wifi_anon_identity",offsetof(AppConfig,wifi_anon_identity),sizeof(((AppConfig*)0)->wifi_anon_identity),Kind::Text,0,2147483647,false},
    {"wifi_validate_ca",offsetof(AppConfig,wifi_validate_ca),sizeof(((AppConfig*)0)->wifi_validate_ca),Kind::Bool,0,2147483647,false},
    {"data_source",offsetof(AppConfig,data_source),sizeof(((AppConfig*)0)->data_source),Kind::Int,0,2,false},
    {"server_url",offsetof(AppConfig,server_url),sizeof(((AppConfig*)0)->server_url),Kind::Text,0,2147483647,true},
    {"auth_token",offsetof(AppConfig,auth_token),sizeof(((AppConfig*)0)->auth_token),Kind::Text,0,2147483647,true},
    {"dexcom_username",offsetof(AppConfig,dexcom_username),sizeof(((AppConfig*)0)->dexcom_username),Kind::Text,0,2147483647,false},
    {"dexcom_password",offsetof(AppConfig,dexcom_password),sizeof(((AppConfig*)0)->dexcom_password),Kind::Text,0,2147483647,true},
    {"dexcom_us",offsetof(AppConfig,dexcom_us),sizeof(((AppConfig*)0)->dexcom_us),Kind::Bool,0,2147483647,false},
    {"poll_interval",offsetof(AppConfig,poll_interval_sec),sizeof(((AppConfig*)0)->poll_interval_sec),Kind::Int,15,3600,false},
    {"brightness",offsetof(AppConfig,brightness),sizeof(((AppConfig*)0)->brightness),Kind::Byte,1,255,false},
    {"auto_brightness",offsetof(AppConfig,auto_brightness),sizeof(((AppConfig*)0)->auto_brightness),Kind::Bool,0,2147483647,false},
    {"show_delta",offsetof(AppConfig,show_delta),sizeof(((AppConfig*)0)->show_delta),Kind::Bool,0,2147483647,false},
    {"use_mmol",offsetof(AppConfig,use_mmol),sizeof(((AppConfig*)0)->use_mmol),Kind::Bool,0,2147483647,false},
    {"thresh_urgent_low",offsetof(AppConfig,thresh_urgent_low),sizeof(((AppConfig*)0)->thresh_urgent_low),Kind::Int,20,600,false},
    {"thresh_low",offsetof(AppConfig,thresh_low),sizeof(((AppConfig*)0)->thresh_low),Kind::Int,20,600,false},
    {"thresh_high",offsetof(AppConfig,thresh_high),sizeof(((AppConfig*)0)->thresh_high),Kind::Int,20,600,false},
    {"thresh_urgent_high",offsetof(AppConfig,thresh_urgent_high),sizeof(((AppConfig*)0)->thresh_urgent_high),Kind::Int,20,600,false},
    {"timezone",offsetof(AppConfig,timezone),sizeof(((AppConfig*)0)->timezone),Kind::Text,0,2147483647,false},
    {"use_24h",offsetof(AppConfig,use_24h),sizeof(((AppConfig*)0)->use_24h),Kind::Bool,0,2147483647,false},
    {"time_display_enabled",offsetof(AppConfig,time_display_enabled),sizeof(((AppConfig*)0)->time_display_enabled),Kind::Bool,0,2147483647,false},
    {"default_mode",offsetof(AppConfig,default_mode),sizeof(((AppConfig*)0)->default_mode),Kind::Int,0,3,false},
    {"ambient_enabled",offsetof(AppConfig,ambient_enabled),sizeof(((AppConfig*)0)->ambient_enabled),Kind::Bool,0,2147483647,false},
    {"ambient_creature",offsetof(AppConfig,ambient_creature),sizeof(((AppConfig*)0)->ambient_creature),Kind::Int,0,1,false},
    {"ambient_seasonal",offsetof(AppConfig,ambient_seasonal),sizeof(((AppConfig*)0)->ambient_seasonal),Kind::Bool,0,2147483647,false},
    {"alert_enabled",offsetof(AppConfig,alert_enabled),sizeof(((AppConfig*)0)->alert_enabled),Kind::Bool,0,2147483647,false},
    {"alert_low",offsetof(AppConfig,alert_low),sizeof(((AppConfig*)0)->alert_low),Kind::Int,20,600,false},
    {"alert_high",offsetof(AppConfig,alert_high),sizeof(((AppConfig*)0)->alert_high),Kind::Int,20,600,false},
    {"alert_snooze_min",offsetof(AppConfig,alert_snooze_min),sizeof(((AppConfig*)0)->alert_snooze_min),Kind::Int,1,120,false},
    {"color_urgent_low",offsetof(AppConfig,color_urgent_low),sizeof(((AppConfig*)0)->color_urgent_low),Kind::UInt,0,16777215,false},
    {"color_low",offsetof(AppConfig,color_low),sizeof(((AppConfig*)0)->color_low),Kind::UInt,0,16777215,false},
    {"color_in_range",offsetof(AppConfig,color_in_range),sizeof(((AppConfig*)0)->color_in_range),Kind::UInt,0,16777215,false},
    {"color_high",offsetof(AppConfig,color_high),sizeof(((AppConfig*)0)->color_high),Kind::UInt,0,16777215,false},
    {"color_urgent_high",offsetof(AppConfig,color_urgent_high),sizeof(((AppConfig*)0)->color_urgent_high),Kind::UInt,0,16777215,false},
    {"color_stale",offsetof(AppConfig,color_stale),sizeof(((AppConfig*)0)->color_stale),Kind::UInt,0,16777215,false},
    {"color_clock",offsetof(AppConfig,color_clock),sizeof(((AppConfig*)0)->color_clock),Kind::UInt,0,16777215,false},
    {"color_weather",offsetof(AppConfig,color_weather),sizeof(((AppConfig*)0)->color_weather),Kind::UInt,0,16777215,false},
    {"night_mode_enabled",offsetof(AppConfig,night_mode_enabled),sizeof(((AppConfig*)0)->night_mode_enabled),Kind::Bool,0,2147483647,false},
    {"night_start_hour",offsetof(AppConfig,night_start_hour),sizeof(((AppConfig*)0)->night_start_hour),Kind::Int,0,23,false},
    {"night_end_hour",offsetof(AppConfig,night_end_hour),sizeof(((AppConfig*)0)->night_end_hour),Kind::Int,0,23,false},
    {"night_brightness",offsetof(AppConfig,night_brightness),sizeof(((AppConfig*)0)->night_brightness),Kind::Byte,1,255,false},
    {"stale_timeout_min",offsetof(AppConfig,stale_timeout_min),sizeof(((AppConfig*)0)->stale_timeout_min),Kind::Int,5,60,false},
    {"weather_enabled",offsetof(AppConfig,weather_enabled),sizeof(((AppConfig*)0)->weather_enabled),Kind::Bool,0,2147483647,false},
    {"weather_api_key",offsetof(AppConfig,weather_api_key),sizeof(((AppConfig*)0)->weather_api_key),Kind::Text,0,2147483647,true},
    {"weather_city",offsetof(AppConfig,weather_city),sizeof(((AppConfig*)0)->weather_city),Kind::Text,0,2147483647,false},
    {"weather_use_f",offsetof(AppConfig,weather_use_f),sizeof(((AppConfig*)0)->weather_use_f),Kind::Bool,0,2147483647,false},
    {"weather_poll_min",offsetof(AppConfig,weather_poll_min),sizeof(((AppConfig*)0)->weather_poll_min),Kind::Int,5,60,false},
    {"date_on_time_screen",offsetof(AppConfig,date_on_time_screen),sizeof(((AppConfig*)0)->date_on_time_screen),Kind::Bool,0,2147483647,false},
    {"date_format",offsetof(AppConfig,date_format),sizeof(((AppConfig*)0)->date_format),Kind::Int,0,2,false},
    {"timer_enabled",offsetof(AppConfig,timer_enabled),sizeof(((AppConfig*)0)->timer_enabled),Kind::Bool,0,2147483647,false},
    {"timer_work_min",offsetof(AppConfig,timer_work_min),sizeof(((AppConfig*)0)->timer_work_min),Kind::Int,1,120,false},
    {"timer_break_min",offsetof(AppConfig,timer_break_min),sizeof(((AppConfig*)0)->timer_break_min),Kind::Int,1,60,false},
    {"timer_long_break_min",offsetof(AppConfig,timer_long_break_min),sizeof(((AppConfig*)0)->timer_long_break_min),Kind::Int,1,60,false},
    {"timer_sessions",offsetof(AppConfig,timer_sessions),sizeof(((AppConfig*)0)->timer_sessions),Kind::Int,1,12,false},
    {"timer_buzzer",offsetof(AppConfig,timer_buzzer),sizeof(((AppConfig*)0)->timer_buzzer),Kind::Bool,0,2147483647,false},
    {"stopwatch_enabled",offsetof(AppConfig,stopwatch_enabled),sizeof(((AppConfig*)0)->stopwatch_enabled),Kind::Bool,0,2147483647,false},
    {"notify_enabled",offsetof(AppConfig,notify_enabled),sizeof(((AppConfig*)0)->notify_enabled),Kind::Bool,0,2147483647,false},
    {"notify_default_duration",offsetof(AppConfig,notify_default_duration),sizeof(((AppConfig*)0)->notify_default_duration),Kind::Int,5,600,false},
    {"notify_allow_buzzer",offsetof(AppConfig,notify_allow_buzzer),sizeof(((AppConfig*)0)->notify_allow_buzzer),Kind::Bool,0,2147483647,false},
    {"sysmon_enabled",offsetof(AppConfig,sysmon_enabled),sizeof(((AppConfig*)0)->sysmon_enabled),Kind::Bool,0,2147483647,false},
    {"sysmon_label",offsetof(AppConfig,sysmon_label),sizeof(((AppConfig*)0)->sysmon_label),Kind::Text,0,2147483647,false},
    {"sysmon_display_mode",offsetof(AppConfig,sysmon_display_mode),sizeof(((AppConfig*)0)->sysmon_display_mode),Kind::Int,0,1,false},
    {"sysmon_warn_pct",offsetof(AppConfig,sysmon_warn_pct),sizeof(((AppConfig*)0)->sysmon_warn_pct),Kind::Int,0,100,false},
    {"sysmon_crit_pct",offsetof(AppConfig,sysmon_crit_pct),sizeof(((AppConfig*)0)->sysmon_crit_pct),Kind::Int,0,100,false},
    {"auto_cycle_enabled",offsetof(AppConfig,auto_cycle_enabled),sizeof(((AppConfig*)0)->auto_cycle_enabled),Kind::Bool,0,2147483647,false},
    {"auto_cycle_sec",offsetof(AppConfig,auto_cycle_sec),sizeof(((AppConfig*)0)->auto_cycle_sec),Kind::Int,3,300,false},
    {"auto_update_enabled",offsetof(AppConfig,auto_update_enabled),sizeof(((AppConfig*)0)->auto_update_enabled),Kind::Bool,0,2147483647,false},
    {"auto_update_hour",offsetof(AppConfig,auto_update_hour),sizeof(((AppConfig*)0)->auto_update_hour),Kind::Int,0,23,false},
    {"countdown_enabled",offsetof(AppConfig,countdown_enabled),sizeof(((AppConfig*)0)->countdown_enabled),Kind::Bool,0,2147483647,false},
    {"countdown_name",offsetof(AppConfig,countdown_name),sizeof(((AppConfig*)0)->countdown_name),Kind::Text,0,2147483647,false},
    {"countdown_target",offsetof(AppConfig,countdown_target),sizeof(((AppConfig*)0)->countdown_target),Kind::ULong,0,2147483647,false},
};
}
void config_schema(JsonArray a) {
 for(const auto& f:fields) {
  if(!strncmp(f.name,"wifi_",5)) continue;
  JsonObject o=a.add<JsonObject>(); o["key"]=f.name;
  o["type"]=f.secret?"secret":f.kind==Kind::Text?"text":f.kind==Kind::Bool?"bool":"int";
  if(f.kind==Kind::Text) o["max_length"]=f.size-1;
  else if(f.kind!=Kind::Bool) { o["min"]=f.lo; o["max"]=f.hi; }
 }
}
void config_public(JsonObject o,const AppConfig& c) {
 for(const auto& f:fields) {
  const char* p=(const char*)&c+f.offset;
  if(f.secret) { o[std::string(f.name)+"_configured"]=*p!=0; continue; }
  switch(f.kind) {
   case Kind::Text:o[f.name]=p;break;
   case Kind::Bool:o[f.name]=*(const bool*)p;break;
   case Kind::Byte:o[f.name]=*(const uint8_t*)p;break;
   case Kind::Int:o[f.name]=*(const int*)p;break;
   case Kind::UInt:o[f.name]=*(const uint32_t*)p;break;
   case Kind::ULong:o[f.name]=*(const unsigned long*)p;break;
  }
 }
}
const char* config_patch(AppConfig& c,JsonObjectConst patch,bool web) {
 if(patch.isNull()) return "invalid_patch";
 for(JsonPairConst pair:patch) {
  const Field* fp=nullptr;
  for(const auto& f:fields) if(!strcmp(pair.key().c_str(),f.name)) { fp=&f;break; }
  if(!fp) return "unsupported_field";
  const Field& f=*fp; JsonVariantConst v=pair.value(); char* p=(char*)&c+f.offset;
  if(f.kind==Kind::Text) {
   if(v.isNull() && f.secret) { *p=0;continue; }
   if(!v.is<const char*>()) return f.name;
   const char* s=v.as<const char*>(); size_t n=strlen(s);
   if(n>=f.size || n!=v.as<JsonString>().size()) return f.name;
   if(web && f.secret && !n) continue;
   if(!strcmp(f.name,"server_url") && n && strncmp(s,"https://",8) && strncmp(s,"http://",7)) return f.name;
   memcpy(p,s,n+1);
  } else if(f.kind==Kind::Bool) {
   if(!v.is<bool>()) return f.name;
   *(bool*)p=v.as<bool>();
  } else {
   long value;
   if(web && !strncmp(f.name,"color_",6) && v.is<const char*>()) {
    const char* s=v.as<const char*>(); if(strlen(s)!=7 || s[0]!='#') return f.name;
    char* end;value=strtol(s+1,&end,16);if(*end) return f.name;
   } else { if(!v.is<long>()) return f.name;value=v.as<long>(); }
   if(value<f.lo || value>f.hi) return f.name;
   switch(f.kind) {
    case Kind::Byte:*(uint8_t*)p=value;break;
    case Kind::Int:*(int*)p=value;break;
    case Kind::UInt:*(uint32_t*)p=value;break;
    case Kind::ULong:*(unsigned long*)p=value;break;
    default:break;
   }
  }
 }
 if(!(c.thresh_urgent_low<=c.thresh_low && c.thresh_low<c.thresh_high && c.thresh_high<=c.thresh_urgent_high)) return "threshold_order";
 if(c.alert_low>=c.alert_high) return "alert_order";
 if(!c.time_display_enabled && c.default_mode==1) c.default_mode=0;
 if(!c.ambient_enabled && c.default_mode==3) c.default_mode=0;
 return nullptr;
}

bool config_source_changed(const AppConfig& a,const AppConfig& b) {
 return a.data_source!=b.data_source || a.dexcom_us!=b.dexcom_us ||
  strcmp(a.dexcom_username,b.dexcom_username) || strcmp(a.dexcom_password,b.dexcom_password) ||
  strcmp(a.server_url,b.server_url) || strcmp(a.auth_token,b.auth_token);
}
