#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "ble_protocol.h"

// The runner includes the actual firmware callbacks below. Only the hardware
// boundary is stubbed, allowing either ordering of NimBLE's connection events.
constexpr uint16_t BLE_HS_CONN_HANDLE_NONE=0xffff;
constexpr int BLE_HS_ENOMEM=6;
struct ble_store_status_event {};
struct NimBLEConnInfo {
 uint16_t handle=1;
 bool encrypted=true,authenticated=true,bonded=true;
 uint8_t keySize=16;
 uint16_t getConnHandle() const {return handle;}
 int getIdAddress() const {return handle;}
 bool isEncrypted() const {return encrypted;}
 bool isAuthenticated() const {return authenticated;}
 bool isBonded() const {return bonded;}
 uint8_t getSecKeySize() const {return keySize;}
};
struct NimBLEServer {
 std::vector<uint16_t> rejected;
 void disconnect(uint16_t handle) {rejected.push_back(handle);}
 void updateConnParams(uint16_t,int,int,int,int) {}
};
struct NimBLEDeviceCallbacks {virtual int onStoreStatus(ble_store_status_event*,void*)=0;};
struct NimBLEServerCallbacks {
 virtual void onConnect(NimBLEServer*,NimBLEConnInfo&)=0;
 virtual void onDisconnect(NimBLEServer*,NimBLEConnInfo&,int)=0;
 virtual uint32_t onPassKeyDisplay()=0;
 virtual void onConfirmPassKey(NimBLEConnInfo&,uint32_t)=0;
 virtual void onAuthenticationComplete(NimBLEConnInfo&)=0;
};
bool storedBond=true;
struct NimBLEDevice {
 static bool isBonded(int) {return storedBond;}
 static unsigned getNumBonds() {return 1;}
 static void injectConfirmPasskey(NimBLEConnInfo&,bool) {}
};
struct Logger {template<typename... Args> void printf(const char*,Args...) {} void println(const char*) {}} Serial;
struct {bool sm_sc_only=true;} ble_hs_cfg;
uint32_t now=1000;
uint32_t millis() {return now;}
uint32_t esp_random() {return 123456;}
struct Guard {~Guard() {}};
NimBLEServer fakeServer;
NimBLEServer* server=&fakeServer;
scble::Receiver receiver;
char response[scble::MaxMessage+1]={};
uint16_t responseID=0,responseLen=0,responseOffset=0,lastID=0;
bool queued=false,responsePending=false;
std::atomic<uint16_t> connection{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint32_t> connectedAt{0},lastActivity{0},sessionEpoch{0};
std::atomic<uint32_t> windowUntil{0},passkeyUntil{0},passkey{0};
std::atomic<bool> secure{false},networkLease{false};
#include "ble_session.inc"
NimBLEServerCallbacks& callbacks=serverCallbacks;

void reset() {
 fakeServer.rejected.clear();connection=BLE_HS_CONN_HANDLE_NONE;secure=false;
 connectedAt=lastActivity=0;sessionEpoch=0;now=1000;
 windowUntil=passkeyUntil=0;storedBond=true;ble_hs_cfg.sm_sc_only=true;
 clearMailbox();
}
int main() {
 NimBLEConnInfo peer;
 reset();
 auto unencrypted=peer;unencrypted.encrypted=false;unencrypted.authenticated=false;
 callbacks.onConnect(server,unencrypted);
 assert(!allowed(unencrypted));
 callbacks.onAuthenticationComplete(peer);
 assert(allowed(peer));assert(sessionEpoch==1);

 reset();callbacks.onAuthenticationComplete(peer);
 callbacks.onConnect(server,peer);
 assert(allowed(peer));assert(sessionEpoch==1);

 // Regression: authentication may precede connect. Requests may already be
 // transferring when the later callback arrives; preserve their session too.
 reset();callbacks.onAuthenticationComplete(peer);
 assert(allowed(peer));assert(connection==peer.handle);
 receiver.used=7;queued=true;responsePending=true;responseLen=9;lastID=3;
 now=1200;callbacks.onConnect(server,peer);
 assert(allowed(peer));assert(sessionEpoch==1);assert(connectedAt==1000);
 assert(receiver.used==7 && queued && responsePending && responseLen==9 && lastID==3);
 assert(fakeServer.rejected.empty());

 // A different peer cannot take over or clear an existing authenticated link.
 auto other=peer;other.handle=2;
 callbacks.onAuthenticationComplete(other);
 callbacks.onConnect(server,other);
 callbacks.onDisconnect(server,other,534);
 assert(allowed(peer));assert(!allowed(other));assert(sessionEpoch==1);
 assert(receiver.used==7 && responsePending);
 assert(fakeServer.rejected==std::vector<uint16_t>({2,2}));

 // Handle reuse after disconnect must not reuse authentication or requests.
 callbacks.onDisconnect(server,peer,534);
 assert(!secure && connection==BLE_HS_CONN_HANDLE_NONE);
 assert(!receiver.used && !queued && !responsePending && responseLen==0 && lastID==0);
 callbacks.onConnect(server,unencrypted);
 assert(!allowed(peer));assert(sessionEpoch==3);
 callbacks.onAuthenticationComplete(peer);assert(allowed(peer));

 // Every security requirement still applies in both callback orders.
 for(int requirement=0;requirement<5;++requirement) {
  for(bool authFirst:{false,true}) {
   reset();auto invalid=peer;
   if(requirement==0) invalid.encrypted=false;
   if(requirement==1) invalid.authenticated=false;
   if(requirement==2) invalid.bonded=false;
   if(requirement==3) invalid.keySize=15;
   if(requirement==4) ble_hs_cfg.sm_sc_only=false;
   if(!authFirst) callbacks.onConnect(server,invalid);
   callbacks.onAuthenticationComplete(invalid);
   if(authFirst) callbacks.onConnect(server,invalid);
   assert(!allowed(invalid));assert(!secure);
   assert(fakeServer.rejected==std::vector<uint16_t>({peer.handle}));
  }
 }
 reset();callbacks.onAuthenticationComplete(peer);
 callbacks.onAuthenticationComplete(unencrypted);
 assert(!allowed(peer));assert(!secure);

 reset();storedBond=false;
 callbacks.onConnect(server,unencrypted);
 assert(connection==BLE_HS_CONN_HANDLE_NONE && !secure);
 assert(fakeServer.rejected==std::vector<uint16_t>({peer.handle}));
 reset();storedBond=false;windowUntil=2000;
 callbacks.onConnect(server,unencrypted);
 assert(connection==peer.handle && !secure && fakeServer.rejected.empty());
 puts("BLE session callbacks: event ordering, mailbox preservation, peer isolation, reconnect and security checks passed");
}
