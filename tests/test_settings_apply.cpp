#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "config_manager.h"
#include "config_patch.h"
#include "config_transaction.h"

AppConfig active={},committed={};
int failure=0,rebuilds=0,refreshes=0,brightness=-1;
bool displayedTime=false;
void config_lock() {}
void config_unlock() {}
AppConfig& config_get() {return active;}
bool config_save() {
    auto outcome=config_transaction([] {return failure!=1;},[] {return failure!=2;},[] {return failure!=3;});
    if(outcome==ConfigCommit::Rejected) active=committed;
    if(outcome==ConfigCommit::Saved) committed=active;
    return outcome==ConfigCommit::Saved;
}
void http_configuration_changed() {++refreshes;}
void engine_rebuild_toggle_order() {++rebuilds;displayedTime=active.time_display_enabled;}
void display_set_brightness(int value) {brightness=value;}
#include "settings_apply.inc"
int main() {
    for(failure=0;failure<4;++failure) {
        active={};active.brightness=20;active.glucose_enabled=true;strcpy(active.timezone,"UTC0");committed=active;
        AppConfig candidate=active;candidate.brightness=80;candidate.time_display_enabled=true;candidate.glucose_enabled=false;strcpy(candidate.timezone,"EST5");
        rebuilds=refreshes=0;brightness=-1;
        assert(settings_apply(candidate)==(failure==0));
        bool rejected=failure==1;
        assert(active.brightness==(rejected?20:80));
        assert(brightness==active.brightness);
        assert(displayedTime==active.time_display_enabled);
        assert(strcmp(getenv("TZ"),active.timezone)==0);
        assert(rebuilds==1 && refreshes==1);
    }
    // A successful cosmetic edit must not provoke an extra provider fetch.
    failure=0;refreshes=0;AppConfig candidate=active;candidate.brightness=40;
    assert(settings_apply(candidate));assert(refreshes==0);
}
