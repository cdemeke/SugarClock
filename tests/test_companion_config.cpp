#include "config_manager.h"
#include "config_patch.h"
#include "companion.h"
#include <cassert>
#include <map>
#include <string>
struct Prefs {
 std::map<std::string,int> values;
 int getInt(const char* key,int fallback) {auto it=values.find(key);return it==values.end()?fallback:it->second;}
 int putInt(const char* key,int value) {values[key]=value;return sizeof(int);}
} prefs;
int main() {
 AppConfig config{};config.glucose_enabled=true;config.thresh_urgent_low=70;config.thresh_low=80;
 config.thresh_high=180;config.thresh_urgent_high=250;config.alert_low=70;config.alert_high=250;
 for(int id=0;id<7;++id) {
  JsonDocument patch;patch["ambient_character"]=id;
  assert(!config_patch(config,patch.as<JsonObjectConst>()) && config.ambient_creature==id);
  JsonDocument output;config_public(output.to<JsonObject>(),config);
  assert(output["ambient_character"].as<int>()==id);
  assert(output["ambient_creature"].as<int>()==(id==1?1:0));
  JsonDocument doc;const AppConfig& cfg=config;
  #include "companion_web.inc"
  assert(doc["ambient_character"].as<int>()==id);
  assert(doc["ambient_creature"].as<int>()==(id==1?1:0));
  bool ok=true;prefs.values={{"untouched",123}};
  #include "companion_save.inc"
  assert(ok && prefs.values["pal_type"]==id && prefs.values["amb_kind"]==(id==1?1:0));
  assert(prefs.values["untouched"]==123);
  config.ambient_creature=-1;
  #include "companion_load.inc"
  assert(config.ambient_creature==id);
 }
 for(int old:{0,1}) {
  prefs.values={{"amb_kind",old}};
  #include "companion_load.inc"
  assert(config.ambient_creature==old);
 }
 for(const char* json:{"{\"ambient_creature\":1,\"ambient_character\":6}","{\"ambient_character\":6,\"ambient_creature\":1}"}) {
  JsonDocument patch;deserializeJson(patch,json);assert(!config_patch(config,patch.as<JsonObjectConst>()) && config.ambient_creature==6);
 }
 for(const char* json:{"{\"ambient_character\":7}","{\"ambient_character\":-1}","{\"ambient_creature\":2}","{\"ambient_character\":true}"}) {
  JsonDocument patch;deserializeJson(patch,json);assert(config_patch(config,patch.as<JsonObjectConst>()));
 }
 JsonDocument schema;config_schema(schema.to<JsonArray>());bool found=false;
 for(JsonObject field:schema.as<JsonArray>()) {
  assert(strcmp(field["key"],"ambient_creature"));
  if(!strcmp(field["key"],"ambient_character")) {found=true;assert(field["max"].as<int>()==6);}
 }
 assert(found);
}
