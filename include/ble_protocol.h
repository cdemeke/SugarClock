#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace scble {
constexpr size_t MaxMessage = 4096;
constexpr size_t Header = 8;
constexpr size_t MaxPacket = 180;
constexpr uint8_t Version = 1;
constexpr const char* Service = "ca7c0001-63a2-4b7c-9a5b-763e4e0c1000";
constexpr const char* Request = "ca7c0002-63a2-4b7c-9a5b-763e4e0c1000";
constexpr const char* Response = "ca7c0003-63a2-4b7c-9a5b-763e4e0c1000";
inline uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
inline void put16(uint8_t* p, uint16_t n) { p[0]=n; p[1]=n>>8; }
inline void header(uint8_t* p,uint8_t flags,uint16_t id,uint16_t offset,uint16_t total) {
    p[0]=Version; p[1]=flags; put16(p+2,id); put16(p+4,offset); put16(p+6,total);
}
enum class Result { More, Complete, Invalid };
struct Receiver {
    uint8_t bytes[MaxMessage+1] = {};
    uint16_t id=0, total=0, used=0;
    uint32_t touched=0;
    void reset() { memset(bytes,0,sizeof(bytes)); id=total=used=0; touched=0; }
    Result accept(const uint8_t* p,size_t n,uint32_t now) {
        if (used && uint32_t(now-touched)>10000) reset();
        if(n<=Header || n>MaxPacket || p[0]!=Version || p[1]!=0) return Result::Invalid;
        uint16_t i=u16(p+2), o=u16(p+4), t=u16(p+6);
        size_t len=n-Header;
        if(!i || !t || t>MaxMessage || o+len>t) return Result::Invalid;
        if(!used) { if(o) return Result::Invalid; id=i; total=t; }
        if(id!=i || total!=t) return Result::Invalid;
        // Exact duplicate fragments are harmless; overlapping/different ones fail.
        if(o<used) return o+len<=used && !memcmp(bytes+o,p+Header,len) ?
            (used==total?Result::Complete:Result::More) : Result::Invalid;
        if(o!=used) return Result::Invalid;
        memcpy(bytes+used,p+Header,len); used+=len; bytes[used]=0; touched=now;
        return used==total?Result::Complete:Result::More;
    }
};
inline bool canAdmit(bool bonded,bool pairingWindow,unsigned bonds) { return bonded || (pairingWindow && bonds<4); }
inline bool authorized(bool encrypted,bool authenticated,bool bonded,bool secureConnections) {
    return encrypted && authenticated && bonded && secureConnections;
}
}
