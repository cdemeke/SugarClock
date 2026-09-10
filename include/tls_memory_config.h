#pragma once

#include "sdkconfig.h"

// Applied to the rebuilt TLS archive and its callers. Encryption, cipher suites,
// verification policy and incoming record capacity remain unchanged.
#ifndef SUGARCLOCK_TLS_TX_BYTES
#define SUGARCLOCK_TLS_TX_BYTES 4096
#endif
#if SUGARCLOCK_TLS_TX_BYTES != 4096 && SUGARCLOCK_TLS_TX_BYTES != 16384
#error "Only the qualified candidate and original-capacity TLS profiles are supported"
#endif
#ifdef CONFIG_MBEDTLS_DYNAMIC_BUFFER
#error "Dynamic TLS buffers need a separate SDK integration and qualification"
#endif
#ifndef CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC
#error "Re-audit the SDK allocator before changing the TLS memory allocation mode"
#endif
#undef CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN
#define CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN 1
#undef CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN
#define CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN 16384
#undef CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN
#define CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN SUGARCLOCK_TLS_TX_BYTES
