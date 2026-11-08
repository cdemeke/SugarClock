#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include "config_journal_layouts.inc"
static_assert(offsetof(AppConfig, night_disable_auto_cycle) == sizeof(PreviousAppConfig),
              "Appended settings must not consume padding from the previous journal");
constexpr uint32_t CONFIG_MAGIC=1234;
AppConfig config{};
struct Prefs {
 std::vector<unsigned char> bytes;
 bool isKey(const char* key) { return !strcmp(key,"pending_v1") && !bytes.empty(); }
 bool remove(const char*) { bytes.clear();return true; }
 size_t getBytesLength(const char*) {return bytes.size();}
 size_t getBytes(const char*,void* out,size_t n) {assert(n<=bytes.size());memcpy(out,bytes.data(),n);return n;}
} prefs;
unsigned saves=0;
bool config_save() {++saves;return true;}
void recover() {
#include "glucose_journal.inc"
}
int main() {
 AppConfig old{};strcpy(old.dexcom_password,"keep-secret");old.alert_enabled=true;old.magic=CONFIG_MAGIC;
 auto bytes=reinterpret_cast<unsigned char*>(&old);
 prefs.bytes.assign(bytes,bytes+offsetof(AppConfig,glucose_enabled));
 config.glucose_enabled=false;recover();
 assert(config.glucose_enabled && config.alert_enabled && saves==1);
 assert(!strcmp(config.dexcom_password,"keep-secret"));
 old.glucose_enabled=false;prefs.bytes.assign(bytes,bytes+sizeof(old));
 recover();assert(!config.glucose_enabled && !config.alert_enabled && saves==2);
 assert(!strcmp(config.dexcom_password,"keep-secret"));
 // The immediately preceding BLE layout retains its disabled state while
 // newly appended settings loaded from NVS survive replay of the old prefix.
 config.ambient_style=2;
 prefs.bytes.assign(bytes,bytes+offsetof(AppConfig,ambient_style));
 recover();assert(!config.glucose_enabled && config.ambient_style==2 && saves==3);
 // A main-era journal must preserve appended defaults, and a new one must
 // restore the pause setting alongside all other settings after interruption.
 const size_t previous_end=offsetof(PreviousAppConfig,libre_patient_name)+sizeof(((PreviousAppConfig*)0)->libre_patient_name);
 for(bool loaded_pause:{false,true}) {
  config.night_disable_auto_cycle=loaded_pause;
  prefs.bytes.assign(bytes,bytes+sizeof(PreviousAppConfig));
  // Padding in a previously persisted object is not a boolean setting.
  for(size_t i=previous_end;i<prefs.bytes.size();++i) prefs.bytes[i]=0xff;
  recover();assert(config.night_disable_auto_cycle==loaded_pause);
 }
 assert(saves==5);
 old.night_disable_auto_cycle=true;prefs.bytes.assign(bytes,bytes+sizeof(old));
 recover();assert(config.night_disable_auto_cycle && saves==6);
 // An unrecognized journal must not be interpreted as a partial configuration.
 prefs.bytes.resize(2);recover();assert(saves==6);
}
