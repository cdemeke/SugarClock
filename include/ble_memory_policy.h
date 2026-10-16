#pragma once
#include <stdint.h>
// Protect pairing and fragmented transfers, not an indefinitely idle phone.
// Even continuous traffic cannot postpone autonomous network work indefinitely.
inline bool ble_network_can_start(bool leased,bool connected,uint32_t idle_ms,uint32_t waiting_ms,
                                  bool pairing=false,bool transfer=false,uint32_t connected_ms=60000) {
    if(leased) return false;
    if(!connected) return true;
    if(waiting_ms>=45000) return true;
    if(pairing || transfer || connected_ms<10000) return false;
    return idle_ms>=1500;
}
