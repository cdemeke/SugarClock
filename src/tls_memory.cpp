#include <Arduino.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_internal.h>
#include <esp_heap_caps.h>
#include "tls_memory.h"

static_assert(MBEDTLS_SSL_IN_CONTENT_LEN == 16384, "Keep full TLS receive capacity");
static_assert(MBEDTLS_SSL_OUT_CONTENT_LEN == SUGARCLOCK_TLS_TX_BYTES, "TLS profile mismatch");

void tls_log_memory_profile() {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);
    uint32_t before = ESP.getFreeHeap();
    int result = mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                            MBEDTLS_SSL_TRANSPORT_STREAM,
                                            MBEDTLS_SSL_PRESET_DEFAULT);
    if (result == 0) result = mbedtls_ssl_setup(&ssl, &config);
    uint32_t allocated = before - ESP.getFreeHeap();
    unsigned incoming_allocation = 0, outgoing_allocation = 0, outgoing = 0;
    if (result == 0) {
        // Inspect real allocations and a function inside the linked library.
        // The MFL negotiation getter is not the allocated receive capacity.
        incoming_allocation = heap_caps_get_allocated_size(ssl.in_buf);
        outgoing_allocation = heap_caps_get_allocated_size(ssl.out_buf);
        outgoing = mbedtls_ssl_get_max_out_record_payload(&ssl);
    }
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    bool profile_ok = result == 0 && outgoing == SUGARCLOCK_TLS_TX_BYTES &&
        incoming_allocation >= MBEDTLS_SSL_IN_BUFFER_LEN && incoming_allocation < MBEDTLS_SSL_IN_BUFFER_LEN + 16 &&
        outgoing_allocation >= MBEDTLS_SSL_OUT_BUFFER_LEN && outgoing_allocation < MBEDTLS_SSL_OUT_BUFFER_LEN + 16;
    Serial.printf("[TLS MEM] rx=%u tx=%u rx_alloc=%u tx_alloc=%u setup_heap=%u released=%d result=%d profile_ok=%d\n",
                  unsigned(MBEDTLS_SSL_IN_CONTENT_LEN), outgoing, incoming_allocation, outgoing_allocation,
                  allocated, int(ESP.getFreeHeap()) - int(before), result, profile_ok);
}
