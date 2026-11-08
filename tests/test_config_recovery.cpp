#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "config_manager.h"
#include "config_transaction.h"
#include "companion.h"

constexpr unsigned CONFIG_MAGIC=0x474C5543;
AppConfig config{},committed{};
bool durable=false;
void config_lock() {}
void config_unlock() {}
struct Preferences {
    bool failJournal=false,failMirror=false,failCleanup=false,failPause=false;
    std::map<std::string,bool> bools;
    std::vector<unsigned char> journal;
    std::map<std::string,std::string> strings;
    size_t putBytes(const char*,const void* data,size_t size) {
        if(failJournal) return 0;
        const auto* bytes=static_cast<const unsigned char*>(data);
        journal.assign(bytes,bytes+size);return size;
    }
    size_t putString(const char* key,const char* value) {strings[key]=value;return strlen(value);}
    std::string getString(const char* key,const char* fallback) {return strings.count(key) ? strings[key]:fallback;}
    template<class T> size_t putUInt(const char*,T) {return 4;}
    template<class T> size_t putULong(const char*,T) {return 4;}
    template<class T> size_t putInt(const char*,T) {return 4;}
    template<class T> size_t putBool(const char* key,T value) {
        if(failPause && !strcmp(key,"acyc_noff")) return 0;
        bools[key]=value;return 1;
    }
    template<class T> size_t putUChar(const char*,T) {return failMirror ? 0:1;}
    bool getBool(const char* key,bool fallback) {return bools.count(key) ? bools[key]:fallback;}
    bool isKey(const char*) {return false;}
    bool remove(const char*) {if(failCleanup) return false;journal.clear();return true;}
} prefs;
struct {void println(const char*) {}} Serial;
#include "config_save.inc"
#include "config_defaults.inc"
void load_cycle_setting() {
#include "config_cycle_load.inc"
}

int main() {
    config_set_defaults();assert(!config.night_disable_auto_cycle);
    config.night_disable_auto_cycle=true;load_cycle_setting();
    assert(!config.night_disable_auto_cycle); // old installations have no key
    config={};config.magic=CONFIG_MAGIC;
    assert(config_save() && !prefs.bools.at("acyc_noff"));
    config.night_disable_auto_cycle=true;
    assert(config_save() && prefs.bools.at("acyc_noff"));
    config.night_disable_auto_cycle=false;load_cycle_setting();
    assert(config.night_disable_auto_cycle);
    prefs.failPause=true;config.night_disable_auto_cycle=false;
    assert(!config_save() && !durable && !committed.night_disable_auto_cycle);
    AppConfig rebooted{};assert(prefs.journal.size()==sizeof(rebooted));
    memcpy(&rebooted,prefs.journal.data(),sizeof(rebooted));
    assert(!rebooted.night_disable_auto_cycle);

    for(bool cleanupFailure:{false,true}) {
        prefs={};config={};committed={};config.magic=CONFIG_MAGIC;config.brightness=20;
        assert(config_save() && durable && committed.brightness==20);
        config.brightness=80;
        prefs.failMirror=!cleanupFailure;prefs.failCleanup=cleanupFailure;
        assert(!config_save() && !durable);
        assert(config.brightness==80 && committed.brightness==80);
        // A newer edit cannot replace the redo journal. Runtime must roll back
        // to the same complete snapshot a reboot would recover, not to 20.
        prefs.failJournal=true;config.brightness=99;
        assert(!config_save() && !durable && config.brightness==80);
        AppConfig rebooted{};assert(prefs.journal.size()==sizeof(rebooted));
        memcpy(&rebooted,prefs.journal.data(),sizeof(rebooted));
        assert(memcmp(&rebooted,&config,sizeof(config))==0);
        prefs.failJournal=prefs.failMirror=prefs.failCleanup=false;
        assert(config_save() && durable && prefs.journal.empty());
        config.brightness=120;prefs.failJournal=true;
        assert(!config_save() && config.brightness==80);
    }
}
