#include "ble_memory_policy.h"
#include "ble_protocol.h"
#include "config_patch.h"
#include "config_transaction.h"
#include "wifi_trial_policy.h"
#include <assert.h>
#include <fstream>
#include <string>
using namespace scble;
int main() {
    assert(ble_network_can_start(false,false,0,0));
    assert(!ble_network_can_start(true,false,1000,60000));
    assert(!ble_network_can_start(false,true,100,2500));
    assert(ble_network_can_start(false,true,1500,2500));
    assert(!ble_network_can_start(false,true,10000,44000,true));
    assert(!ble_network_can_start(false,true,10000,44000,false,true));
    assert(!ble_network_can_start(false,true,2000,5000,false,false,5000));
    assert(ble_network_can_start(false,true,2000,10000,false,false,10000));
    assert(ble_network_can_start(false,true,0,45000,true,true,0));
    // A 4 KiB transfer at MTU 23 spans 342 fragments. It must not be
    // disconnected by network scheduling between fragments after 2.5 seconds.
    for(unsigned fragment=0;fragment<342;++fragment)
        assert(!ble_network_can_start(false,true,100,fragment*100,false,true));

 assert(wifi_trial_recovery(true,false)==WifiTrialRecovery::ReconnectSaved);
 assert(wifi_trial_recovery(true,true)==WifiTrialRecovery::RetrySavedInPortal);
 assert(wifi_trial_recovery(false,true)==WifiTrialRecovery::StayInPortal);
 assert(wifi_trial_recovery(false,false)==WifiTrialRecovery::StayInPortal);
 assert(!wifi_trial_has_address(true,false));assert(!wifi_trial_has_address(false,true));assert(wifi_trial_has_address(true,true));
 // Simulate interruption at every mirrored key. Recovery must replay the complete
 // candidate; an unknown legacy key remains untouched.
 for(int failAt=0;failAt<5;++failAt) {
  int saved[4]={1,2,3,999}, candidate[3]={7,8,9};bool journal=false;
  auto result=config_transaction([&] {journal=failAt!=0;return journal;},[&] {
   for(int i=0;i<3;++i) {if(failAt==i+1)return false;saved[i]=candidate[i];}return true;
  },[&] {journal=false;return true;});
  if(failAt==0) {assert(result==ConfigCommit::Rejected);assert(saved[0]==1);}
  else if(failAt<4) {assert(result==ConfigCommit::RecoveryPending && journal);
   assert(config_transaction([] {return true;},[&] {for(int i=0;i<3;++i)saved[i]=candidate[i];return true;},[&] {journal=false;return true;})==ConfigCommit::Saved);
  } else assert(result==ConfigCommit::Saved);
  assert(saved[3]==999);
 }
 assert(config_transaction([] {return true;},[] {return true;},[] {return false;})==ConfigCommit::RecoveryPending);

 assert(canAdmit(true,false,4));assert(!canAdmit(false,false,0));assert(!canAdmit(false,true,4));assert(canAdmit(false,true,0));
 Receiver r;
 uint8_t p[MaxPacket];header(p,0,1,0,4096);memset(p+8,'A',172);
 assert(r.accept(p,180,1)==Result::More);
 assert(r.accept(p,180,2)==Result::More);
 p[8]='B';assert(r.accept(p,180,3)==Result::Invalid);p[8]='A';
 for(unsigned offset=172;offset<4096;offset+=172) {
  size_t len=std::min<unsigned>(172,4096-offset);header(p,0,1,offset,4096);
  assert(r.accept(p,len+8,4)==(offset+len==4096?Result::Complete:Result::More));
 }
 assert(r.used==4096);r.reset();header(p,0,1,0,4097);assert(r.accept(p,9,1)==Result::Invalid);
 header(p,0,1,0,20);assert(r.accept(p,18,1)==Result::More);header(p,0,1,10,20);assert(r.accept(p,18,10002)==Result::Invalid);
 for(int bits=0;bits<16;++bits) assert(authorized(bits&1,bits&2,bits&4,bits&8)==(bits==15));
 AppConfig config={};config.thresh_urgent_low=70;config.thresh_low=80;config.thresh_high=180;config.thresh_urgent_high=250;config.alert_low=70;config.alert_high=250;
 strcpy(config.dexcom_password,"keep-test-password");strcpy(config.wifi_eap_password,"keep-enterprise");config.wifi_security=1;config.wifi_validate_ca=true;
 JsonDocument patch;deserializeJson(patch,"{\"brightness\":77}");assert(!config_patch(config,patch.as<JsonObjectConst>()));assert(config.brightness==77);
 AppConfig same=config;same.brightness=12;assert(!config_source_changed(config,same));same.dexcom_password[0]=0;assert(config_source_changed(config,same));
 assert(!strcmp(config.dexcom_password,"keep-test-password"));assert(config.wifi_security==1 && config.wifi_validate_ca);
 deserializeJson(patch,"{\"brightness\":256}");assert(config_patch(config,patch.as<JsonObjectConst>()));
 deserializeJson(patch,"{\"brightness\":true}");assert(config_patch(config,patch.as<JsonObjectConst>()));
 deserializeJson(patch,"{\"future_field\":1}");assert(config_patch(config,patch.as<JsonObjectConst>()));
 deserializeJson(patch,"{\"dexcom_password\":null}");assert(!config_patch(config,patch.as<JsonObjectConst>()));assert(!config.dexcom_password[0]);
 deserializeJson(patch,"{\"alert_low\":300}");assert(!strcmp(config_patch(config,patch.as<JsonObjectConst>()),"alert_order"));config.alert_low=70;
 JsonDocument output;config_public(output.to<JsonObject>(),config);assert(output["wifi_eap_password"].isNull());assert(output["wifi_eap_password_configured"].as<bool>());
 std::ifstream f("protocol/fixtures/frames.json");JsonDocument fixtures;assert(!deserializeJson(fixtures,f));
 for(JsonObject fixture:fixtures.as<JsonArray>()) {
  std::string hex=fixture["hex"].as<std::string>();std::string bytes;
  for(size_t i=0;i<hex.size();i+=2) bytes+=char(std::stoi(hex.substr(i,2),nullptr,16));
  if(fixture["valid"].as<bool>() && uint8_t(bytes[1])==0) {r.reset();assert(r.accept((const uint8_t*)bytes.data(),bytes.size(),0)==Result::Complete);}
  else if(!fixture["valid"].as<bool>()) {r.reset();assert(r.accept((const uint8_t*)bytes.data(),bytes.size(),0)==Result::Invalid);}
 }
}
