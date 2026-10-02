#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
struct AppConfig {char password[16];bool alert_enabled;uint32_t magic;bool glucose_enabled;int ambient_style;};
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
 AppConfig old{};strcpy(old.password,"keep-secret");old.alert_enabled=true;old.magic=CONFIG_MAGIC;
 auto bytes=reinterpret_cast<unsigned char*>(&old);
 prefs.bytes.assign(bytes,bytes+offsetof(AppConfig,glucose_enabled));
 config.glucose_enabled=false;recover();
 assert(config.glucose_enabled && config.alert_enabled && saves==1);
 assert(!strcmp(config.password,"keep-secret"));
 old.glucose_enabled=false;prefs.bytes.assign(bytes,bytes+sizeof(old));
 recover();assert(!config.glucose_enabled && !config.alert_enabled && saves==2);
 assert(!strcmp(config.password,"keep-secret"));
 // The immediately preceding BLE layout retains its disabled state while
 // newly appended settings loaded from NVS survive replay of the old prefix.
 config.ambient_style=2;
 prefs.bytes.assign(bytes,bytes+offsetof(AppConfig,ambient_style));
 recover();assert(!config.glucose_enabled && config.ambient_style==2 && saves==3);
 // An unrecognized journal must not be interpreted as a partial configuration.
 prefs.bytes.resize(2);recover();assert(saves==3);
}
