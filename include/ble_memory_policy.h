#pragma once
#include <stdint.h>
// Give an active GATT exchange a brief chance to finish, then prioritize the
// clock's autonomous network work. This never waits inside a BLE callback.
inline bool ble_network_can_start(bool leased,bool connected,uint32_t idle_ms,uint32_t waiting_ms,bool pairing=false) {
    return !leased && (!connected || (pairing ? waiting_ms>=30000 : idle_ms>=300 || waiting_ms>=2500));
}
