#pragma once
#include <stddef.h>
#include <stdio.h>

namespace scble {
inline void discovery_name(char* output, size_t size, const char* identity) {
    // ESP.getEfuseMac() stores the MAC bytes in little-endian order. The first
    // six identity characters vary per device; the last six are the vendor OUI.
    snprintf(output, size, "SugarClock-%.6s", identity);
}

template<class Advertisement>
bool configure_advertisement(Advertisement& advertisement, const char* service, const char* name) {
    // Flags + 128-bit service + full name exceed legacy advertising's 31 bytes.
    // NimBLE routes setName to scan response only after this is enabled.
    advertisement.enableScanResponse(true);
    return advertisement.addServiceUUID(service) && advertisement.setName(name);
}
}
