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

 assert(canAdmit(true,false));assert(!canAdmit(false,false));assert(canAdmit(false,true));
 {
  // Full storage replaces the phone that has gone longest without authenticating.
  BondPeer a{{0,1}},b{{0,2}},c{{0,3}},d{{0,4}},e{{1,5}};
  BondPeer bonded[MaxBonds]={a,b,c,d};
  BondOrder order;
  assert(order.touch(a,bonded,1));assert(order.touch(b,bonded,2));
  assert(order.touch(c,bonded,3));assert(order.touch(d,bonded,4));
  assert(!order.touch(d,bonded,4)); // Repeat use of the newest phone needs no save.
  assert(leastRecentBond(order,bonded,4,e)==0);
  assert(order.touch(a,bonded,4));assert(leastRecentBond(order,bonded,4,e)==1);
  // NVS slot reuse puts the newest bond first after a reboot; order still wins.
  BondPeer rebooted[MaxBonds]={e,c,d,a};
  assert(order.touch(e,rebooted,4));
  assert(order.count==4 && samePeer(order.peers[0],c) && samePeer(order.peers[3],e));
  assert(leastRecentBond(order,rebooted,4,b)==1);
  // Bonds saved before ordering existed are replaced first; never the incoming phone.
  BondPeer legacy[MaxBonds]={c,b,d,e};
  assert(leastRecentBond(order,legacy,4,a)==1);
  assert(leastRecentBond(order,legacy,1,c)==-1);
 }
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
 AppConfig config={};config.glucose_enabled=true;config.thresh_urgent_low=70;config.thresh_low=80;config.thresh_high=180;config.thresh_urgent_high=250;config.alert_low=70;config.alert_high=250;
 strcpy(config.dexcom_password,"keep-test-password");strcpy(config.wifi_eap_password,"keep-enterprise");config.wifi_security=1;config.wifi_validate_ca=true;
 JsonDocument patch;deserializeJson(patch,"{\"brightness\":77}");assert(!config_patch(config,patch.as<JsonObjectConst>()));assert(config.brightness==77);
 AppConfig same=config;same.brightness=12;assert(!config_source_changed(config,same));same.dexcom_password[0]=0;assert(config_source_changed(config,same));
 assert(!strcmp(config.dexcom_password,"keep-test-password"));assert(config.wifi_security==1 && config.wifi_validate_ca);
 deserializeJson(patch,"{\"brightness\":256}");assert(config_patch(config,patch.as<JsonObjectConst>()));
 deserializeJson(patch,"{\"brightness\":true}");assert(config_patch(config,patch.as<JsonObjectConst>()));
 deserializeJson(patch,"{\"future_field\":1}");assert(config_patch(config,patch.as<JsonObjectConst>()));
 deserializeJson(patch,"{\"dexcom_password\":null}");assert(!config_patch(config,patch.as<JsonObjectConst>()));assert(!config.dexcom_password[0]);
 deserializeJson(patch,"{\"alert_low\":300}");assert(!strcmp(config_patch(config,patch.as<JsonObjectConst>()),"alert_order"));config.alert_low=70;

 config.alert_enabled=true;strcpy(config.dexcom_password,"saved-password");
 AppConfig enabled=config;
 deserializeJson(patch,"{\"glucose_enabled\":false}");
 assert(!config_patch(config,patch.as<JsonObjectConst>()));
 assert(!config.glucose_enabled && !config.alert_enabled);
 assert(!strcmp(config.dexcom_password,"saved-password"));
 assert(config_source_changed(enabled,config));
 deserializeJson(patch,"{\"alert_enabled\":true}");
 assert(!config_patch(config,patch.as<JsonObjectConst>()) && !config.alert_enabled);
 deserializeJson(patch,"{\"glucose_enabled\":true}");
 assert(!config_patch(config,patch.as<JsonObjectConst>()));
 assert(config.glucose_enabled && !config.alert_enabled);
 JsonDocument output;config_public(output.to<JsonObject>(),config);assert(output["wifi_eap_password"].isNull());assert(output["wifi_eap_password_configured"].as<bool>());
 strcpy(config.libre_email,"test@example.invalid");strcpy(config.libre_password,"private");
 strcpy(config.libre_region,"us");strcpy(config.libre_patient_id,"patient");
 config.ambient_style=2;config.glucose_only_when_low=true;
 deserializeJson(patch,"{\"brightness\":78}");assert(!config_patch(config,patch.as<JsonObjectConst>()));
 assert(!strcmp(config.libre_patient_id,"patient") && config.ambient_style==2 && config.glucose_only_when_low);
 output.clear();config_public(output.to<JsonObject>(),config);
 assert(output["libre_password"].isNull() && output["libre_email"].isNull());
 assert(output["libre_password_configured"].as<bool>() && output["libre_email_configured"].as<bool>());
 strcpy(config.libre_patient_name,"Patient");
 // Rotating the password keeps the selected person on a multi-person account.
 deserializeJson(patch,"{\"data_source\":3,\"libre_password\":\"replacement\",\"libre_patient_id\":\"\"}");
 same=config;assert(!config_patch(config,patch.as<JsonObjectConst>(),true));
 assert(config_source_changed(same,config) && !strcmp(config.libre_password,"replacement"));
 assert(!strcmp(config.libre_region,"us") && !strcmp(config.libre_patient_id,"patient") && !strcmp(config.libre_patient_name,"Patient"));
 deserializeJson(patch,"{\"libre_password\":\"rotated\"}");assert(!config_patch(config,patch.as<JsonObjectConst>()));
 assert(!strcmp(config.libre_patient_id,"patient"));
 deserializeJson(patch,"{\"libre_email\":\"other@example.invalid\"}");assert(!config_patch(config,patch.as<JsonObjectConst>()));
 assert(!config.libre_region[0] && !config.libre_patient_id[0] && !config.libre_patient_name[0]);
 strcpy(config.libre_region,"us");strcpy(config.libre_patient_id,"patient");strcpy(config.libre_patient_name,"Alice");
 deserializeJson(patch,"{\"libre_password\":null}");assert(!config_patch(config,patch.as<JsonObjectConst>()));
 assert(!config.libre_password[0] && !strcmp(config.libre_region,"us") && !strcmp(config.libre_patient_id,"patient"));
 // A new person must come from this account's tested list, and brings its name.
 LibrePatientSnapshot people("other@example.invalid",2);
 strcpy(people.people[0].id,"patient");strcpy(people.people[0].name,"Alice");
 strcpy(people.people[1].id,"other");strcpy(people.people[1].name,"Bob");
 LibrePatientSnapshot stranger("someone@example.invalid",2);stranger.people=people.people;
 deserializeJson(patch,"{\"libre_patient_id\":\"other\"}");same=config;
 assert(!strcmp(config_patch(config,patch.as<JsonObjectConst>()),"libre_patient_id"));config=same;
 assert(!strcmp(config_patch(config,patch.as<JsonObjectConst>(),true,&stranger),"libre_patient_id"));config=same;
 assert(!config_patch(config,patch.as<JsonObjectConst>(),true,&people));
 assert(!strcmp(config.libre_patient_id,"other") && !strcmp(config.libre_patient_name,"Bob"));
 deserializeJson(patch,"{\"libre_patient_id\":\"unknown\"}");same=config;
 assert(!strcmp(config_patch(config,patch.as<JsonObjectConst>(),true,&people),"libre_patient_id"));config=same;
 // The web form always resends the current person; that needs no list.
 deserializeJson(patch,"{\"libre_patient_id\":\"other\",\"brightness\":79}");
 assert(!config_patch(config,patch.as<JsonObjectConst>(),true) && !strcmp(config.libre_patient_name,"Bob"));
 deserializeJson(patch,"{\"libre_patient_id\":null}");assert(!config_patch(config,patch.as<JsonObjectConst>()));
 assert(!config.libre_patient_id[0] && !config.libre_patient_name[0]);
 // A new email may be a different account: forget its region and person.
 strcpy(config.libre_patient_id,"patient");strcpy(config.libre_patient_name,"Alice");
 deserializeJson(patch,"{\"libre_email\":\"new@example.invalid\",\"libre_patient_id\":\"other\"}");
 assert(!config_patch(config,patch.as<JsonObjectConst>(),false,&people));
 assert(!config.libre_region[0] && !config.libre_patient_id[0] && !config.libre_patient_name[0]);
 std::ifstream f("protocol/fixtures/frames.json");JsonDocument fixtures;assert(!deserializeJson(fixtures,f));
 for(JsonObject fixture:fixtures.as<JsonArray>()) {
  std::string hex=fixture["hex"].as<std::string>();std::string bytes;
  for(size_t i=0;i<hex.size();i+=2) bytes+=char(std::stoi(hex.substr(i,2),nullptr,16));
  if(fixture["valid"].as<bool>() && uint8_t(bytes[1])==0) {r.reset();assert(r.accept((const uint8_t*)bytes.data(),bytes.size(),0)==Result::Complete);}
  else if(!fixture["valid"].as<bool>()) {r.reset();assert(r.accept((const uint8_t*)bytes.data(),bytes.size(),0)==Result::Invalid);}
 }
}
