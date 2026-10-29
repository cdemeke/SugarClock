// Native integration client for the exact pinned Mbed TLS source. Hardware and
// ESP allocator behavior are checked separately on the USB clock.
#include <stdio.h>
#include <string.h>
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_net_context net;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;
    mbedtls_x509_crt ca;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);
    mbedtls_net_init(&net);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);
    mbedtls_x509_crt_init(&ca);
    int result = 1, rc;
    const char *stage = "entropy seed";
    const unsigned char purpose[] = "sugarclock-tls-test";
    if (mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, purpose, sizeof(purpose))) goto done;
    stage = "CA parse";
    if ((rc = mbedtls_x509_crt_parse_file(&ca, argv[2]))) { fprintf(stderr, "CA parse %d\n", rc); goto done; }
    stage = "config defaults";
    if (mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                   MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) goto done;
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&config, &ca, NULL);
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &rng);
    mbedtls_ssl_conf_min_version(&config, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&config, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    stage = "SSL setup";
    if (mbedtls_ssl_setup(&ssl, &config)) goto done;
    stage = "buffer limits";
    if (MBEDTLS_SSL_IN_CONTENT_LEN != 16384 ||
        mbedtls_ssl_get_max_out_record_payload(&ssl) != 4096) goto done;
    stage = "hostname";
    if (mbedtls_ssl_set_hostname(&ssl, argv[3])) goto done;
    stage = "TCP connect";
    if (mbedtls_net_connect(&net, "127.0.0.1", argv[1], MBEDTLS_NET_PROTO_TCP)) goto done;
    mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, NULL);
    stage = "handshake";
    do { rc = mbedtls_ssl_handshake(&ssl); }
    while (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (rc || mbedtls_ssl_get_verify_result(&ssl)) {
        fprintf(stderr, "certificate/handshake rejected: %d\n", rc);
        result = 3;
        goto done;
    }
    stage = "data transfer";
    unsigned char outgoing[20000], incoming[32768];
    for (size_t i = 0; i < sizeof(outgoing); ++i) outgoing[i] = i % 251;
    size_t sent = 0, received = 0;
    unsigned chunks = 0;
    while (sent < sizeof(outgoing)) {
        rc = mbedtls_ssl_write(&ssl, outgoing + sent, sizeof(outgoing) - sent);
        if (rc <= 0 || rc > 4096) goto done;
        sent += rc;
        ++chunks;
    }
    while (received < sizeof(incoming)) {
        rc = mbedtls_ssl_read(&ssl, incoming + received, sizeof(incoming) - received);
        if (rc <= 0) goto done;
        received += rc;
    }
    for (size_t i = 0; i < sizeof(incoming); ++i)
        if (incoming[i] != (i * 7) % 251) goto done;
    if (chunks < 5) goto done;
    printf("Verified TLS 1.2: 20000 outgoing / 32768 incoming bytes, %u writes, certificate verified\n", chunks);
    result = 0;
done:
    if (result) fprintf(stderr, "Failed during %s\n", stage);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_net_free(&net);
    mbedtls_x509_crt_free(&ca);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
    return result;
}
