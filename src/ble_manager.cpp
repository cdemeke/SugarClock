#include "ble_manager.h"
#include "ble_protocol.h"
#include "ble_memory_policy.h"
#include "config_patch.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "http_client.h"
#include "net_check.h"
#include "display.h"
#include "glucose_engine.h"
#include "buzzer.h"
#include "notify_engine.h"
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <atomic>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
NimBLEServer* server=nullptr;
NimBLECharacteristic* tx=nullptr;
StaticSemaphore_t storage;
SemaphoreHandle_t mutex;
struct Guard { Guard(){xSemaphoreTake(mutex,portMAX_DELAY);} ~Guard(){xSemaphoreGive(mutex);} };
scble::Receiver receiver;
char response[scble::MaxMessage+1]={};
char work[scble::MaxMessage+1]={};
uint16_t responseID=0,responseLen=0,responseOffset=0,lastID=0;
uint8_t lastHash[32]={};
std::atomic<uint16_t> connection{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint32_t> windowUntil{0},passkeyUntil{0},passkey{0},connectedAt{0},lastActivity{0};
std::atomic<bool> secure{false},resetRequested{false};
bool enabled=false,queued=false,working=false,suspended=false;
std::atomic<bool> networkLease{false};
uint32_t networkWaitingSince=0;
uint16_t workID=0,workLen=0;
std::atomic<uint32_t> sessionEpoch{0};
uint32_t workEpoch=0;
char identity[20],name[26];
uint32_t bootID;
bool windowOpen() { uint32_t end=windowUntil;return end && int32_t(end-millis())>0; }
bool allowed(NimBLEConnInfo& c) {
 return secure && connection==c.getConnHandle() &&
   scble::authorized(c.isEncrypted(),c.isAuthenticated(),c.isBonded(),ble_hs_cfg.sm_sc_only) && c.getSecKeySize()==16;
}
void disconnect() { uint16_t c=connection;if(server && c!=BLE_HS_CONN_HANDLE_NONE) server->disconnect(c); }
void clearMailbox() { receiver.reset();memset(response,0,sizeof(response));responseID=responseLen=responseOffset=lastID=0;queued=false; }
class DeviceCallbacks:public NimBLEDeviceCallbacks {
 int onStoreStatus(ble_store_status_event*,void*) override { return BLE_HS_ENOMEM; } // Never evict an owner's bond implicitly.
} deviceCallbacks;
class ServerCallbacks:public NimBLEServerCallbacks {
 void onConnect(NimBLEServer* s,NimBLEConnInfo& c) override {
  connection=c.getConnHandle();secure=false;connectedAt=lastActivity=millis();
  { Guard g; ++sessionEpoch;clearMailbox(); }
  if(!scble::canAdmit(NimBLEDevice::isBonded(c.getIdAddress()),windowOpen(),NimBLEDevice::getNumBonds())) { s->disconnect(c.getConnHandle());return; }
  s->updateConnParams(c.getConnHandle(),24,48,0,400);
 }
 void onDisconnect(NimBLEServer*,NimBLEConnInfo&,int) override {
  secure=false;connection=BLE_HS_CONN_HANDLE_NONE;passkeyUntil=0;
  Guard g;++sessionEpoch;clearMailbox(); // queued but unexecuted work is canceled; Wi-Fi/OTA already started continues.
 }
 uint32_t onPassKeyDisplay() override {
  if(!windowOpen() || NimBLEDevice::getNumBonds()>=4) { disconnect();return esp_random()%1000000; }
  uint32_t code=esp_random()%1000000;passkey=code;passkeyUntil=millis()+30000;return code;
 }
 void onConfirmPassKey(NimBLEConnInfo& c,uint32_t) override { NimBLEDevice::injectConfirmPasskey(c,false); }
 void onAuthenticationComplete(NimBLEConnInfo& c) override {
  if(!scble::authorized(c.isEncrypted(),c.isAuthenticated(),c.isBonded(),ble_hs_cfg.sm_sc_only) || c.getSecKeySize()!=16) { disconnect();return; }
  secure=true;passkeyUntil=0;
 }
} serverCallbacks;
class Characteristics:public NimBLECharacteristicCallbacks {
 void onWrite(NimBLECharacteristic* ch,NimBLEConnInfo& c) override {
  if(!allowed(c)) { disconnect();return; }
  auto value=ch->getValue();ch->setValue(""); // do not retain credentials in GATT attribute
  Guard g;lastActivity=millis();
  const uint8_t* p=value.data();size_t n=value.size();
  if(n==scble::Header && p[0]==1 && p[1]==2) {
   if(scble::u16(p+2)==responseID && scble::u16(p+6)==responseLen && scble::u16(p+4)<=responseLen) responseOffset=scble::u16(p+4);
   return;
  }
  if(queued || working) {disconnect();return;}
  auto result=receiver.accept(p,n,millis());
  if(result==scble::Result::Invalid) { receiver.reset();disconnect();return; }
  if(result==scble::Result::Complete) queued=true;
 }
 void onRead(NimBLECharacteristic* ch,NimBLEConnInfo& c) override {
  if(!allowed(c)) { ch->setValue("");disconnect();return; }
  Guard g;lastActivity=millis();
  uint8_t packet[scble::MaxPacket];
  size_t capacity=std::min<size_t>(scble::MaxPacket,c.getMTU()-3);
  size_t len=std::min<size_t>(capacity-scble::Header,responseLen-responseOffset);
  scble::header(packet,1,responseID,responseOffset,responseLen);
  memcpy(packet+scble::Header,response+responseOffset,len);ch->setValue(packet,len+scble::Header);
 }
} characteristics;
void status(JsonObject o) {
 o["wifi"]=wifi_get_status();o["trial"]=wifi_trial_status_str();
 o["trial_detail"]=wifi_trial_detail();o["network_saved"]=config_has_wifi();o["configuration_saved"]=config_is_durable();
 o["internet_dns"]=int(netcheck_dns());o["provider_reachable"]=int(netcheck_data());
 o["data_received"]=http_has_ever_received();o["data_age_ms"]=http_time_since_last_reading();
 o["provider_fetching"]=http_is_fetching();o["provider_http"]=http_get_last_response_code();o["provider_failures"]=http_get_failure_count();
 o["free_heap"]=ESP.getFreeHeap();o["min_heap"]=ESP.getMinFreeHeap();
 o["largest_block"]=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
 o["bonds"]=NimBLEDevice::getNumBonds();o["pairing_open"]=windowOpen();
 OtaStatusSnapshot s;ota_get_status(s);JsonObject ota=o["ota"].to<JsonObject>();
 ota["state"]=ota_state_name(s.state);ota["progress"]=s.progress;ota["current_version"]=s.current_version;
 ota["available_version"]=s.available_version;ota["error"]=s.last_error;ota["deferral"]=s.safety_reason;
 ota["pending_verification"]=s.pending_verification;
}
void execute(JsonDocument& in,JsonDocument& out) {
 out["v"]=1;out["id"]=workID;out["state"]="applied";
 const char* op=in["op"]|"";
 if(!in["v"].is<unsigned>() || !in["id"].is<unsigned>() || in["v"].as<int>()!=1 || in["id"].as<unsigned>()!=workID) { out["error"]="unsupported_protocol";return; }
 if(!strcmp(op,"hello")) {
  out["device_id"]=identity;out["boot_id"]=bootID;out["name"]=name;
  out["firmware"]=SUGARCLOCK_VERSION;out["hardware"]="ulanzi-tc001-esp32-4mb";
  out["ota_disconnect"]=true;out["max_message"]=scble::MaxMessage;out["max_packet"]=scble::MaxPacket;
  auto a=out["capabilities"].to<JsonArray>();
  for(auto cap:{"settings.patch","wifi.trial","wifi.enterprise.preserve","ota.signed_wifi","bonds.physical_reset","schema","network.pause"}) a.add(cap);
 } else if(!strcmp(op,"settings.get")) {
  ConfigGuard guard;config_public(out["settings"].to<JsonObject>(),config_get());out["saved"]=config_is_durable();
 } else if(!strcmp(op,"schema.get")) {
  // Page to stay below the fixed message budget.
  JsonDocument all;config_schema(all.to<JsonArray>());int page=in["page"]|0;
  if(page<0 || page>10) { out["error"]="invalid_page";return; }
  auto fields=out["fields"].to<JsonArray>();for(size_t i=page*16;i<all.size() && i<size_t((page+1)*16);++i) fields.add(all[i]);
  out["more"]=(page+1)*16<int(all.size());
 } else if(!strcmp(op,"status.get")) status(out["status"].to<JsonObject>());
 else if(!strcmp(op,"settings.patch")) {
  if(ota_is_busy()) { out["error"]="busy";return; }
  ConfigGuard guard;AppConfig candidate=config_get();JsonObjectConst patch=in["patch"].as<JsonObjectConst>();
  for(JsonPairConst p:patch) if(!strncmp(p.key().c_str(),"wifi_",5)) { out["error"]="use_wifi_trial";return; }
  const char* error=config_patch(candidate,patch);if(error) { out["error"]="validation";out["field"]=error;return; }
  bool sourceChanged=config_source_changed(config_get(),candidate);
  config_get()=candidate;if(sourceChanged) http_configuration_changed();if(!config_save()) { out["error"]="persistence_failed";return; }

  engine_rebuild_toggle_order();if(!candidate.auto_brightness) display_set_brightness(candidate.brightness);
  setenv("TZ",candidate.timezone,1);tzset();out["saved"]=true;
 } else if(!strcmp(op,"wifi.scan")) {
  if(ota_is_busy()) { out["error"]="busy";return; }
  wifi_scan_start();out["state"]="queued";
 } else if(!strcmp(op,"wifi.results")) {
  out["scanning"]=wifi_scan_in_progress();auto a=out["networks"].to<JsonArray>();
  for(int i=0;i<wifi_scan_count();++i) { const auto* e=wifi_scan_get(i);auto n=a.add<JsonObject>();n["ssid"]=e->ssid;n["rssi"]=e->rssi;n["enterprise"]=e->enterprise;n["auth"]=e->enc; }
 } else if(!strcmp(op,"wifi.trial")) {
  if(ota_is_busy()) { out["error"]="busy";return; }
  WifiTrialParams p={};
  { ConfigGuard guard;AppConfig candidate=config_get();auto patch=in["patch"].as<JsonObjectConst>();
    for(JsonPairConst f:patch) if(strncmp(f.key().c_str(),"wifi_",5)) { out["error"]="validation";return; }
    const char* err=config_patch(candidate,patch);if(err) {out["error"]="validation";out["field"]=err;return;}
    if(!strlen(candidate.wifi_ssid) || strlen(candidate.wifi_ssid)>32 || (candidate.wifi_security==1 && (!strlen(candidate.wifi_identity) || (candidate.wifi_validate_ca && !config_ca_exists())))) { out["error"]="wifi_credentials_or_ca_required";return; }
    strncpy(p.ssid,candidate.wifi_ssid,sizeof(p.ssid)-1);p.security=candidate.wifi_security;p.eap_method=candidate.wifi_eap_method;
    strncpy(p.password,p.security?candidate.wifi_eap_password:candidate.wifi_password,sizeof(p.password)-1);
    strncpy(p.identity,candidate.wifi_identity,sizeof(p.identity)-1);strncpy(p.anon_identity,candidate.wifi_anon_identity,sizeof(p.anon_identity)-1);p.validate_ca=candidate.wifi_validate_ca;
  }
  if(!wifi_trial_start(p)) out["error"]="busy";else out["state"]="queued";
  memset(&p,0,sizeof(p));
 } else if(!strcmp(op,"ota.check") || !strcmp(op,"ota.install")) {
  OtaRequestResult r=!strcmp(op,"ota.check")?ota_request_check():ota_request_install(true);
  if(r==OTA_REQUEST_QUEUED) out["state"]="queued";
  else out["error"]=r==OTA_REQUEST_BUSY?"busy":r==OTA_REQUEST_UNSAFE?"deferred":r==OTA_REQUEST_NO_UPDATE?"no_update":"internal";
 } else if(!strcmp(op,"bonds.reset")) { out["error"]="physical_action_required"; }
 else out["error"]="unsupported_operation";
}
}
void ble_init() {
 if(!mutex) {mutex=xSemaphoreCreateMutexStatic(&storage);bootID=esp_random();}
 snprintf(identity,sizeof(identity),"%012llX",ESP.getEfuseMac());snprintf(name,sizeof(name),"SugarClock-%.6s",identity+6);
 if(!NimBLEDevice::init(name)) { Serial.println("[BLE] Unavailable; normal clock operation continues");return; }
 NimBLEDevice::setDeviceCallbacks(&deviceCallbacks);
 if(config_bond_reset_pending() && NimBLEDevice::deleteAllBonds()) config_bond_reset_finished();
 NimBLEDevice::setSecurityAuth(true,true,true);ble_hs_cfg.sm_sc_only=1;
 NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID);
 NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID);
 NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);NimBLEDevice::setMTU(185);
 server=NimBLEDevice::createServer();if(!server) { NimBLEDevice::deinit(true);return; }
 server->setCallbacks(&serverCallbacks,false);server->advertiseOnDisconnect(false);
 auto* service=server->createService(scble::Service);
 if(!service) { NimBLEDevice::deinit(true);server=nullptr;return; }
 auto* rx=service->createCharacteristic(scble::Request,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_ENC|NIMBLE_PROPERTY::WRITE_AUTHEN,scble::MaxPacket);
 tx=service->createCharacteristic(scble::Response,NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::READ_ENC|NIMBLE_PROPERTY::READ_AUTHEN,scble::MaxPacket);
 if(!service || !rx || !tx) { NimBLEDevice::deinit(true);server=nullptr;return; }
 rx->setCallbacks(&characteristics);tx->setCallbacks(&characteristics);if(!server->start()) {NimBLEDevice::deinit(true);server=nullptr;return;}
 auto* adv=NimBLEDevice::getAdvertising();adv->addServiceUUID(scble::Service);adv->setName(name);adv->enableScanResponse(true);
 Serial.printf("[BLE MEM] free=%u min=%u largest=%u\n",ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 enabled=true;if(!config_has_wifi() && NimBLEDevice::getNumBonds()==0) ble_pairing_window();adv->start();
}
void ble_pairing_window() { if(enabled) windowUntil=millis()+120000; }
void ble_reset_bonds() { if(enabled) resetRequested=true; }
bool ble_is_connected() { return connection!=BLE_HS_CONN_HANDLE_NONE; }
void ble_suspend_for_ota() {
 if(!enabled) return;
 disconnect();NimBLEDevice::stopAdvertising();
 if(NimBLEDevice::deinit(true)) {enabled=false;suspended=true;server=nullptr;tx=nullptr;connection=BLE_HS_CONN_HANDLE_NONE;secure=false;}
}
bool ble_acquire_network() {
 uint32_t now=millis();
 if(!networkWaitingSince) networkWaitingSince=now;
 if(!ble_network_can_start(networkLease,ble_is_connected(),now-lastActivity,now-networkWaitingSince,!secure)) return false;
 if(enabled) {
  Serial.println("[BLE] Pausing for network TLS; reconnect after request");
  ble_suspend_for_ota();
  if(enabled) return false;
 }
 networkWaitingSince=0;networkLease=true;return true;
}
void ble_release_network() {networkLease=false;}
bool ble_network_is_busy() {return networkLease;}
void ble_loop() {
 if(suspended && !ota_is_busy() && !networkLease) {suspended=false;ble_init();}
 if(!enabled) return;
 if(resetRequested) {
  disconnect();if(ble_is_connected()) return;
  NimBLEDevice::deleteAllBonds();resetRequested=false;ble_pairing_window();
 }
 uint32_t now=millis();
 if(ble_is_connected() && ((!secure && now-connectedAt>45000) || now-lastActivity>60000 || now-connectedAt>600000)) disconnect();
 if(!ble_is_connected() && !ota_is_busy() && !NimBLEDevice::getAdvertising()->isAdvertising()) NimBLEDevice::startAdvertising();
 if(ota_is_busy()) NimBLEDevice::stopAdvertising();
 // Copy bounded work while locked, then release before configuration or network queues.
 { Guard g;if(!queued || working) return;working=true;queued=false;workID=receiver.id;workLen=receiver.total;workEpoch=sessionEpoch;
   memcpy(work,receiver.bytes,receiver.total+1);receiver.reset(); }
 uint8_t hash[32];mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(work),workLen,hash,0);
 { Guard g;if(workID<=lastID) { working=false;memset(work,0,sizeof(work));if(workID!=lastID || memcmp(hash,lastHash,32)!=0) disconnect();else responseOffset=0;return; } }
 JsonDocument in,out;
 auto err=deserializeJson(in,work,workLen,DeserializationOption::NestingLimit(6));
 if(err || memchr(work,0,workLen)) {out["v"]=1;out["id"]=workID;out["error"]="invalid_json";}
 else execute(in,out);
 if(!out["error"].isNull()) out["state"]="failed";
 { Guard g;if(workEpoch!=sessionEpoch) {working=false;memset(work,0,sizeof(work));return;}
   responseOffset=0;responseID=lastID=workID;memcpy(lastHash,hash,32);
   if(measureJson(out)>scble::MaxMessage) {out.clear();out["v"]=1;out["id"]=workID;out["state"]="failed";out["error"]="response_too_large";}
   responseLen=serializeJson(out,response,sizeof(response));working=false; }
 memset(work,0,sizeof(work));
}
void ble_render() {
 if(!enabled || ota_is_busy() || buzzer_is_active() || notify_is_urgent()) return;
 const auto& reading=http_get_reading();const auto& cfg=config_get();
 if(reading.valid && (reading.glucose<cfg.thresh_urgent_low || reading.glucose>cfg.thresh_urgent_high)) return;
 uint32_t until=passkeyUntil;
 if(until && int32_t(until-millis())>0) {
  // Six 3x5 digits fit in 24 pixels; no scrolling or truncation of the pairing code.
  static const uint16_t digits[]={0x7B6F,0x2492,0x73E7,0x73CF,0x5BC9,0x79CF,0x79EF,0x7249,0x7BEF,0x7BCF};
  char code[7];snprintf(code,sizeof(code),"%06u",unsigned(passkey));display_clear();display_set_transition_level(255);
  for(int d=0;d<6;++d) for(int y=0;y<5;++y) for(int x=0;x<3;++x)
   if(digits[code[d]-'0'] & (1 << (14-y*3-x))) display_draw_pixel(4+d*4+x,1+y,display_color(0,220,220));
  display_show();
 } else if(windowOpen() && (millis()%4000)<600) {display_clear();display_draw_text("PAIR",3,0,display_color(0,200,200));display_show();}
}
