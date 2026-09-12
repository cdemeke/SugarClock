#ifndef NIGHTSCOUT_TEST_SHA1_H
#define NIGHTSCOUT_TEST_SHA1_H
#include <cstddef>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
inline int mbedtls_sha1_ret(const unsigned char* input, size_t size, unsigned char output[20]) {
    return CC_SHA1(input, static_cast<CC_LONG>(size), output) ? 0 : -1;
}
#else
#include <openssl/sha.h>
inline int mbedtls_sha1_ret(const unsigned char* input, size_t size, unsigned char output[20]) {
    return SHA1(input, size, output) ? 0 : -1;
}
#endif
#endif
