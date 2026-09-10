#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
struct AppConfig {char password[16];bool alert_enabled;uint32_t magic;bool glucose_enabled;};
constexpr uint32_t CONFIG_MAGIC=1234;
AppConfig config{};
struct Prefs {
 std::vector<unsigned char> bytes;
 size_t getBytesLength(const char*) {return bytes.size();}
 size_t getBytes(const char*,void* out,size_t n) {assert(n<=bytes.size());memcpy(out,bytes.data(),n);return n;}
} prefs;
unsigned saves=0;
void config_save() {++saves;}
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
 // An unrecognized journal must not be interpreted as a partial configuration.
 prefs.bytes.resize(2);recover();assert(saves==2);
}
