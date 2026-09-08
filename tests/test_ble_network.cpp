#include <cassert>
#include <cstdint>
#include "ble_memory_policy.h"
struct Logger {
 template<typename... T> void printf(const char*,T...) {}
 void println(const char*) {}
} Serial;
struct { unsigned getFreeHeap() { return 100000; } } ESP;
constexpr int MALLOC_CAP_8BIT=1;
unsigned heap_caps_get_largest_free_block(int) {return 50000;}
uint32_t now=20000;
uint32_t millis() {return now;}
bool connected=true,enabled=true,networkLease=false,secure=true;
bool queued=false,working=false,responsePending=false;
void* mutex=nullptr;
struct Guard {};
struct {unsigned used=0;} receiver;
uint32_t lastActivity=1000,connectedAt=1000,networkWaitingSince=0,networkReleasedAt=0;
unsigned suspensions=0;
bool ble_is_connected() {return connected;}
void ble_suspend_for_ota() {++suspensions;enabled=false;connected=false;}
#include "ble_network.inc"
int main() {
 // The production lease path keeps one network worker at a time in both modes.
 assert(ble_acquire_network());
 assert(networkLease);
 assert(enabled==bool(SUGARCLOCK_BLE_COEXIST_TEST));
 assert(connected==bool(SUGARCLOCK_BLE_COEXIST_TEST));
 assert(suspensions==unsigned(!SUGARCLOCK_BLE_COEXIST_TEST));
 assert(!ble_acquire_network());
 ble_release_network();assert(!networkLease && networkReleasedAt==now);
 // Protection for an in-progress authenticated transfer remains in place.
 enabled=connected=true;mutex=&connected;responsePending=true;lastActivity=now;
 assert(!ble_acquire_network());
 // Bounded deferral cannot starve readings, and the coexistence path still
 // must not clear a pending response when that deadline arrives.
 now+=45000;
 assert(ble_acquire_network());assert(responsePending);
 assert(enabled==bool(SUGARCLOCK_BLE_COEXIST_TEST));
 ble_release_network();assert(!networkLease);
}
