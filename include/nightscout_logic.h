#ifndef NIGHTSCOUT_LOGIC_H
#define NIGHTSCOUT_LOGIC_H

#include <stddef.h>
#include "nightscout_client.h"

static const size_t NIGHTSCOUT_MAX_BODY_BYTES = 16384;
static const uint32_t NIGHTSCOUT_MIN_EPOCH = 1577836800UL; // 2020-01-01
static const uint32_t NIGHTSCOUT_MAX_FUTURE_SEC = 300;
static const uint32_t NIGHTSCOUT_MAX_DELTA_GAP_SEC = 600;

bool nightscout_validate_config(const NightscoutConfig& config, char* error, size_t size);
bool nightscout_build_url(const NightscoutConfig& config, char* url, size_t size,
                         char* error, size_t error_size);
typedef bool (*NightscoutSha1)(const char* input, size_t size, uint8_t digest[20]);
bool nightscout_build_auth(const NightscoutConfig& config, char* header, size_t size,
                          NightscoutSha1 hash, char* error, size_t error_size);
// Parses the same ArduinoJson code on firmware and host. Does not mutate live
// clock state. A valid UTC clock is required; dateString is not a fallback.
bool nightscout_parse(const char* json, size_t size, uint32_t now_epoch,
                      NightscoutResult& result);

#endif
