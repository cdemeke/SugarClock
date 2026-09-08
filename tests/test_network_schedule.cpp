#include <cassert>
#include "network_schedule.h"
int main() {
 using namespace scnet;
 constexpr uint32_t epoch=2000000000;
 assert(dexcom_timestamp("/Date(2000000000000)/")==epoch);
 assert(dexcom_timestamp("Date(2000000000123+0000)")==epoch);
 for(const char* bad : {"", "Date()", "Date(-1)", "Date(2000000000000oops)",
      "Date(20000000000000000000)", "/Date(2000000000000)", "Date(2000000000000)+0000",
      "Date(2000000000000+00)", "Date(2000000000000)junk", "Date(1)"})
  assert(dexcom_timestamp(bad)==0);
 assert(dexcom_timestamp(nullptr)==0);

 DexcomSchedule s;
 assert(s.ready(0));
 // A reading 20 seconds old: aim 15 seconds after the next five-minute sample.
 assert(s.complete(100,epoch,epoch-20,true,60)==295);
 assert(!s.ready(295099));assert(s.ready(295100));
 // A manual fetch before that time retains the timestamp-based target.
 assert(s.complete(20100,epoch+20,epoch-20,true,60)==275);
 // The expected sample is late: two short retries, then a steady two minutes.
 assert(s.complete(295100,epoch+295,epoch-20,true,60)==30);
 assert(s.complete(325100,epoch+325,epoch-20,true,60)==60);
 assert(s.complete(385100,epoch+385,epoch-20,true,60)==120);
 assert(s.complete(505100,epoch+505,epoch-20,true,60)==120);
 // A newly published sample restores alignment immediately.
 assert(s.complete(505100,epoch+505,epoch+300,true,60)==110);
 assert(s.complete(600000,epoch+600,epoch+300,false,60)==60);
 assert(s.complete(660000,epoch+660,epoch+300,false,60)==120);
 assert(s.complete(780000,epoch+780,epoch+300,false,60)==300);
 assert(s.complete(1080000,epoch+1080,epoch+900,true,60)==135);
 // Invalid/missing/future dates and an unset clock use bounded fallback polling.
 assert(s.complete(0,0,epoch,true,60)==60);
 assert(s.complete(0,epoch,0,true,270)==270);
 assert(s.complete(0,epoch,epoch+1,true,3600)==300);
 assert(s.complete(0,epoch,epoch-3601,true,1)==15);
 // Monotonic deadlines survive millis() rollover and NTP changes after receipt.
 assert(s.complete(UINT32_MAX-1000,epoch,epoch,true,60)==315);
 assert(!s.ready(1000));assert(s.ready(313999));
 s={};assert(s.ready(0));

 assert(management_interval(120)==300);
 assert(management_interval(450)==450);
 assert(management_interval(UINT32_MAX)==600);
 // Existing pauses can be reused up to a minute early; idle radio waits a minute.
 assert(!management_ready(239999,300000,true,false));
 assert(management_ready(240000,300000,true,false));
 assert(!management_ready(300000,300000,false,false));
 assert(management_ready(360000,300000,false,false));
 assert(!management_ready(360000,300000,false,false,true));
 assert(management_ready(500000,300000,true,false,true));
 assert(management_ready(600000,300000,false,false,true));
 // Retry and circuit-open deadlines must never be shortened for batching.
 assert(!management_ready(299999,300000,true,true));
 assert(management_ready(300000,300000,true,true));
 assert(!management_ready(300000,300000,false,true));
 assert(management_ready(360000,300000,false,true));
 assert(management_ready(500,UINT32_MAX-500,true,true));
 assert(!management_ready(UINT32_MAX-1000,500,true,true));
}
