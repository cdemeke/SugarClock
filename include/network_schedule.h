#pragma once
#include <stdint.h>
#include <string.h>

namespace scnet {
inline bool due(uint32_t now, uint32_t deadline) { return int32_t(now-deadline)>=0; }

// Dexcom's JSON date is milliseconds since the epoch, optionally with a zone
// suffix. Reject malformed dates instead of reusing the previous reading's date.
inline uint32_t dexcom_timestamp(const char* text) {
 if(!text) return 0;
 bool slash=*text=='/';if(slash) ++text;
 if(strncmp(text,"Date(",5)) return 0;
 text+=5;uint64_t value=0;unsigned digits=0;
 while(*text>='0' && *text<='9') {
  if(++digits>13) return 0;
  value=value*10+unsigned(*text++-'0');
 }
 if(!digits) return 0;
 if(*text=='+' || *text=='-') {
  ++text;
  for(unsigned i=0;i<4;++i) {if(*text<'0' || *text>'9') return 0;++text;}
 }
 if(*text++!=')') return 0;
 if(slash && *text++!='/') return 0;
 if(*text || value<1577836800000ULL || value>=4102444800000ULL) return 0;
 return uint32_t(value/1000);
}

struct DexcomSchedule {
 uint32_t deadline=0, lastTimestamp=0;
 unsigned late=0, failures=0;
 bool scheduled=false;
 bool ready(uint32_t now) const {return !scheduled || due(now,deadline);}
 uint32_t complete(uint32_t now, uint32_t epoch, uint32_t timestamp,
                   bool success, uint32_t fallbackSeconds) {
  uint32_t seconds;
  if(!success) {
   if(failures<3) ++failures;
   seconds=failures==1 ? 60 : failures==2 ? 120 : 300;
  } else {
   failures=0;
   if(epoch<1577836800UL || epoch>=4102444800UL ||
      timestamp<1577836800UL || timestamp>epoch || epoch-timestamp>3600) {
    seconds=fallbackSeconds<15 ? 15 : fallbackSeconds>300 ? 300 : fallbackSeconds;
    lastTimestamp=0;late=0;
   } else {
    if(timestamp>lastTimestamp) {lastTimestamp=timestamp;late=0;}
    int64_t wait=int64_t(timestamp)+315-epoch;
    if(wait>0 && timestamp==lastTimestamp) seconds=wait<15 ? 15 : uint32_t(wait);
    else {if(late<3) ++late;seconds=late==1 ? 30 : late==2 ? 60 : 120;}
   }
  }
  deadline=now+seconds*1000;scheduled=true;return seconds;
 }
};

// Prefer the tail of an existing radio pause. An unavailable glucose source
// must not starve management; it gets its own turn after a bounded extra minute.
inline bool management_ready(uint32_t now,uint32_t target,bool pausedWindow,bool retry,
                             bool glucoseExpected=false) {
 if(!pausedWindow && glucoseExpected && !due(now,target+300000)) return false;
 if(retry) return (pausedWindow && due(now,target)) || due(now,target+60000);
 return (pausedWindow && int32_t(target-now)<=60000) || due(now,target+60000);
}
inline uint32_t management_interval(uint32_t requested) {
 return requested<300 ? 300 : requested>600 ? 600 : requested;
}
}
