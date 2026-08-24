#ifndef OTA_MANIFEST_H
#define OTA_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#define OTA_MANIFEST_MAX_BYTES 8192
#define OTA_CANONICAL_MAX_BYTES 1024

struct OtaManifest {
    uint32_t schema;
    char product[32];
    char hardware[64];
    char channel[16];
    char version[24];
    char minimum_ota_version[24];
    uint32_t size;
    char sha256[65];
    char firmware_url[384];
    char key_id[40];
    char signature[512];
    char published_at[40];
};

bool ota_manifest_parse(const char* json, size_t length, OtaManifest& out,
                        char* error, size_t error_size);
bool ota_manifest_canonicalize(const OtaManifest& manifest, char* output,
                               size_t output_size, size_t* output_length);
bool ota_manifest_verify_signature(const OtaManifest& manifest,
                                   char* error, size_t error_size);
bool ota_manifest_validate_identity_and_formats(const OtaManifest& manifest,
                                                char* error, size_t error_size);
bool ota_manifest_validate_offer(const OtaManifest& manifest,
                                 const char* current_version,
                                 size_t inactive_partition_size,
                                 char* error, size_t error_size);

#endif
