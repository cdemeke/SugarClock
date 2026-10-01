#include "ble_discovery.h"
#include <cassert>
#include <cstring>
#include <string>
// Model legacy advertisement budgets and NimBLE's name destination selection.
struct Advertisement {
 bool scan=false, failService=false, failName=false;
 size_t primary=3, response=0;
 std::string advertisedName;
 void enableScanResponse(bool value) {scan=value;}
 bool addServiceUUID(const char*) {
  if(failService || primary+18>31) return false;
  primary+=18;return true;
 }
 bool setName(const char* value) {
  size_t& payload=scan?response:primary;
  if(failName || payload+strlen(value)+2>31) return false;
  payload+=strlen(value)+2;advertisedName=value;return true;
 }
};
static bool enabled=true,suspended=false,resetRequested=false;
static unsigned now=1000,windowUntil=0;
unsigned millis() {return now;}
#include "ble_admission.inc"
int main() {
 char first[26],second[26];
 scble::discovery_name(first,sizeof(first),"DC20F8319D48");
 scble::discovery_name(second,sizeof(second),"AB21F8319D48");
 assert(!strcmp(first,"SugarClock-DC20F8"));assert(strcmp(first,second));
 Advertisement a;assert(scble::configure_advertisement(a,"service",first));
 assert(a.primary==21 && a.response==19 && a.advertisedName==first);
 Advertisement b;b.failService=true;assert(!scble::configure_advertisement(b,"service",first));
 Advertisement c;c.failName=true;assert(!scble::configure_advertisement(c,"service",first));
 ble_pairing_window();assert(windowUntil==121000);
 // A physical gesture during a network pause must survive radio reinitialization.
 enabled=false;suspended=true;now=2000;ble_pairing_window();ble_reset_bonds();
 assert(windowUntil==122000 && resetRequested);
 suspended=false;resetRequested=false;now=3000;ble_pairing_window();ble_reset_bonds();
 assert(windowUntil==122000 && !resetRequested);
}
