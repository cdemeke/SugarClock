#include "ota_manifest.h"
#include "ota_public_keys.h"
#include "semver.h"

#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <stdio.h>
#include <string.h>

#ifndef SUGARCLOCK_HARDWARE_ID
#define SUGARCLOCK_HARDWARE_ID "ulanzi-tc001-esp32-4mb"
#endif
#ifndef SUGARCLOCK_CHANNEL
#define SUGARCLOCK_CHANNEL "stable"
#endif

static bool fail(char* error, size_t error_size, const char* message) {
    if (error && error_size) {
        snprintf(error, error_size, "%s", message);
    }
    return false;
}

static bool copy_field(JsonVariantConst value, char* dest, size_t dest_size,
                       const char* field, char* error, size_t error_size) {
    if (!value.is<const char*>()) {
        char message[64];
        snprintf(message, sizeof(message), "missing_%s", field);
        return fail(error, error_size, message);
    }
    const char* source = value.as<const char*>();
    size_t length = strlen(source);
    if (length == 0 || length >= dest_size) {
        char message[64];
        snprintf(message, sizeof(message), "invalid_%s", field);
        return fail(error, error_size, message);
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = static_cast<unsigned char>(source[i]);
        if (c < 0x20 || c == 0x7f) {
            char message[64];
            snprintf(message, sizeof(message), "invalid_%s", field);
            return fail(error, error_size, message);
        }
    }
    memcpy(dest, source, length + 1);
    return true;
}

static bool is_lower_hex_sha256(const char* value) {
    if (!value || strlen(value) != 64) return false;
    for (size_t i = 0; i < 64; ++i) {
        char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static bool valid_key_id(const char* value) {
    if (!value || !*value) return false;
    for (const char* p = value; *p; ++p) {
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return false;
    }
    return true;
}

static bool valid_published_at(const char* value) {
    if (!value || strlen(value) != 20) return false;
    for (size_t i = 0; i < 20; ++i) {
        if (i == 4 || i == 7) { if (value[i] != '-') return false; }
        else if (i == 10) { if (value[i] != 'T') return false; }
        else if (i == 13 || i == 16) { if (value[i] != ':') return false; }
        else if (i == 19) { if (value[i] != 'Z') return false; }
        else if (value[i] < '0' || value[i] > '9') return false;
    }
    return true;
}

bool ota_manifest_parse(const char* json, size_t length, OtaManifest& out,
                        char* error, size_t error_size) {
    memset(&out, 0, sizeof(out));
    if (!json || length == 0) return fail(error, error_size, "empty_manifest");
    if (length > OTA_MANIFEST_MAX_BYTES) return fail(error, error_size, "manifest_too_large");

    JsonDocument doc;
    DeserializationError json_error = deserializeJson(doc, json, length);
    if (json_error || !doc.is<JsonObject>()) return fail(error, error_size, "invalid_json");

    if (!doc["schema"].is<uint32_t>() || !doc["size"].is<uint32_t>()) {
        return fail(error, error_size, "invalid_numeric_field");
    }
    out.schema = doc["schema"].as<uint32_t>();
    out.size = doc["size"].as<uint32_t>();

    return copy_field(doc["product"], out.product, sizeof(out.product), "product", error, error_size) &&
           copy_field(doc["hardware"], out.hardware, sizeof(out.hardware), "hardware", error, error_size) &&
           copy_field(doc["channel"], out.channel, sizeof(out.channel), "channel", error, error_size) &&
           copy_field(doc["version"], out.version, sizeof(out.version), "version", error, error_size) &&
           copy_field(doc["minimum_ota_version"], out.minimum_ota_version, sizeof(out.minimum_ota_version), "minimum_version", error, error_size) &&
           copy_field(doc["sha256"], out.sha256, sizeof(out.sha256), "sha256", error, error_size) &&
           copy_field(doc["firmware_url"], out.firmware_url, sizeof(out.firmware_url), "firmware_url", error, error_size) &&
           copy_field(doc["key_id"], out.key_id, sizeof(out.key_id), "key_id", error, error_size) &&
           copy_field(doc["signature"], out.signature, sizeof(out.signature), "signature", error, error_size) &&
           copy_field(doc["published_at"], out.published_at, sizeof(out.published_at), "published_at", error, error_size);
}

bool ota_manifest_canonicalize(const OtaManifest& m, char* output,
                               size_t output_size, size_t* output_length) {
    if (!output || output_size == 0) return false;
    int written = snprintf(output, output_size,
        "sugarclock-ota-v1\n"
        "product=%s\n"
        "hardware=%s\n"
        "channel=%s\n"
        "version=%s\n"
        "minimum_ota_version=%s\n"
        "size=%lu\n"
        "sha256=%s\n"
        "firmware_url=%s\n"
        "key_id=%s\n",
        m.product, m.hardware, m.channel, m.version, m.minimum_ota_version,
        static_cast<unsigned long>(m.size), m.sha256, m.firmware_url, m.key_id);
    if (written < 0 || static_cast<size_t>(written) >= output_size) return false;
    if (output_length) *output_length = static_cast<size_t>(written);
    return true;
}

bool ota_manifest_verify_signature(const OtaManifest& manifest,
                                   char* error, size_t error_size) {
    const char* public_key = nullptr;
    for (unsigned i = 0; i < OTA_PUBLIC_KEY_COUNT; ++i) {
        if (strcmp(manifest.key_id, OTA_PUBLIC_KEYS[i].key_id) == 0) {
            public_key = OTA_PUBLIC_KEYS[i].pem;
            break;
        }
    }
    if (!public_key) return fail(error, error_size, "unknown_key_id");

    char canonical[OTA_CANONICAL_MAX_BYTES];
    size_t canonical_length = 0;
    if (!ota_manifest_canonicalize(manifest, canonical, sizeof(canonical), &canonical_length)) {
        return fail(error, error_size, "canonical_payload_too_large");
    }

    uint8_t signature[256];
    size_t signature_length = 0;
    int rc = mbedtls_base64_decode(signature, sizeof(signature), &signature_length,
        reinterpret_cast<const unsigned char*>(manifest.signature), strlen(manifest.signature));
    if (rc != 0 || signature_length != sizeof(signature)) {
        return fail(error, error_size, "invalid_signature_encoding");
    }

    uint8_t digest[32];
    rc = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
        reinterpret_cast<const unsigned char*>(canonical), canonical_length, digest);
    if (rc != 0) return fail(error, error_size, "signature_hash_failed");

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    rc = mbedtls_pk_parse_public_key(&pk,
        reinterpret_cast<const unsigned char*>(public_key), strlen(public_key) + 1);
    if (rc == 0) {
        rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                               signature, signature_length);
    }
    mbedtls_pk_free(&pk);
    if (rc != 0) return fail(error, error_size, "invalid_signature");
    return true;
}

bool ota_manifest_validate_offer(const OtaManifest& m,
                                 const char* current_version,
                                 size_t inactive_partition_size,
                                 char* error, size_t error_size) {
    return ota_manifest_validate_offer_for_channel(
        m, SUGARCLOCK_CHANNEL, current_version, inactive_partition_size, error, error_size);
}

bool ota_manifest_validate_offer_for_channel(const OtaManifest& m,
                                             const char* expected_channel,
                                             const char* current_version,
                                             size_t inactive_partition_size,
                                             char* error, size_t error_size) {
    if (!ota_manifest_validate_identity_and_formats_for_channel(
            m, expected_channel, error, error_size)) return false;

    SemVer offered = SemVer::parse(m.version);
    SemVer current = SemVer::parse(current_version);
    SemVer minimum = SemVer::parse(m.minimum_ota_version);
    if (!current.valid) return fail(error, error_size, "invalid_current_version");
    if (offered <= current) return fail(error, error_size, "not_newer");
    if (current < minimum) return fail(error, error_size, "minimum_version_not_met");
    if (m.size == 0 || static_cast<size_t>(m.size) > inactive_partition_size) {
        return fail(error, error_size, "firmware_too_large");
    }
    return true;
}

bool ota_manifest_validate_identity_and_formats(const OtaManifest& m,
                                                char* error, size_t error_size) {
    return ota_manifest_validate_identity_and_formats_for_channel(
        m, SUGARCLOCK_CHANNEL, error, error_size);
}

bool ota_manifest_validate_identity_and_formats_for_channel(const OtaManifest& m,
                                                            const char* expected_channel,
                                                            char* error, size_t error_size) {
    if (m.schema != 1) return fail(error, error_size, "wrong_schema");
    if (strcmp(m.product, "sugarclock") != 0) return fail(error, error_size, "wrong_product");
    if (strcmp(m.hardware, SUGARCLOCK_HARDWARE_ID) != 0) return fail(error, error_size, "wrong_hardware");
    if (!expected_channel ||
        (strcmp(expected_channel, "stable") != 0 && strcmp(expected_channel, "preview") != 0) ||
        strcmp(m.channel, expected_channel) != 0) {
        return fail(error, error_size, "wrong_channel");
    }

    SemVer offered = SemVer::parse(m.version);
    SemVer minimum = SemVer::parse(m.minimum_ota_version);
    if (!offered.valid || !minimum.valid) return fail(error, error_size, "invalid_version");
    if (m.size == 0) return fail(error, error_size, "invalid_size");
    if (!is_lower_hex_sha256(m.sha256)) return fail(error, error_size, "invalid_sha256");
    if (strncmp(m.firmware_url, "https://", 8) != 0) return fail(error, error_size, "firmware_url_not_https");
    if (strchr(m.firmware_url, '#') || strchr(m.firmware_url, '@') || strchr(m.firmware_url, ' ')) {
        return fail(error, error_size, "invalid_firmware_url");
    }
    if (!valid_key_id(m.key_id)) return fail(error, error_size, "invalid_key_id");
    if (!valid_published_at(m.published_at)) return fail(error, error_size, "invalid_published_at");
    return true;
}
