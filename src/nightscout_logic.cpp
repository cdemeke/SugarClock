#include "nightscout_logic.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

namespace {
bool fail(char* out, size_t size, const char* message) {
    if (out && size) snprintf(out, size, "%s", message);
    return false;
}

TrendType direction(const char* text) {
    if (!text) return TREND_UNKNOWN;
    if (!strcmp(text, "DoubleUp")) return TREND_RISING_FAST;
    if (!strcmp(text, "SingleUp") || !strcmp(text, "FortyFiveUp")) return TREND_RISING;
    if (!strcmp(text, "Flat")) return TREND_FLAT;
    if (!strcmp(text, "SingleDown") || !strcmp(text, "FortyFiveDown")) return TREND_FALLING;
    if (!strcmp(text, "DoubleDown")) return TREND_FALLING_FAST;
    return TREND_UNKNOWN;
}

struct Entry { int glucose; uint32_t timestamp; TrendType trend; };
}

bool nightscout_validate_config(const NightscoutConfig& config, char* error, size_t size) {
    if (error && size) error[0] = '\0';
    const size_t len = strnlen(config.url, sizeof(config.url));
    if (!len) return fail(error, size, "Enter your Nightscout site URL");
    if (len == sizeof(config.url)) return fail(error, size, "Nightscout URL is too long");
    if (strncmp(config.url, "https://", 8))
        return fail(error, size, "Nightscout requires an https:// site URL");
    const char* host = config.url + 8;
    const char* path = strchr(host, '/');
    const size_t authority_len = path ? static_cast<size_t>(path - host) : strlen(host);
    if (!authority_len || *host == ':' || *host == '.')
        return fail(error, size, "Nightscout URL needs a hostname");
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(config.url[i]);
        if (c <= 32 || c >= 127 || c == '\\' || c == '@' || c == '?' || c == '#')
            return fail(error, size, "Use a site URL without credentials, query, or fragment");
    }
    if (path && strstr(path, "/api/"))
        return fail(error, size, "Enter the site URL, without the /api endpoint");
    if (config.auth_mode < 0 || config.auth_mode > 2)
        return fail(error, size, "Choose a valid Nightscout authentication method");
    const size_t cred_len = strnlen(config.credential, sizeof(config.credential));
    if (cred_len == sizeof(config.credential))
        return fail(error, size, "Nightscout credential is too long");
    if (config.auth_mode && !cred_len)
        return fail(error, size, "Enter a Nightscout token or API secret");
    for (size_t i = 0; i < cred_len; ++i) {
        unsigned char c = static_cast<unsigned char>(config.credential[i]);
        if (c < 32 || c == 127)
            return fail(error, size, "Nightscout credential contains invalid characters");
    }
    return true;
}

bool nightscout_build_url(const NightscoutConfig& config, char* url, size_t size,
                         char* error, size_t error_size) {
    if (!nightscout_validate_config(config, error, error_size)) return false;
    size_t len = strlen(config.url);
    while (len && config.url[len - 1] == '/') --len;
    const int written = snprintf(url, size, "%.*s/api/v1/entries/sgv.json?count=3",
                                 static_cast<int>(len), config.url);
    if (written < 0 || static_cast<size_t>(written) >= size)
        return fail(error, error_size, "Nightscout request URL is too long");
    return true;
}

bool nightscout_build_auth(const NightscoutConfig& config, char* header, size_t size,
                          NightscoutSha1 hash, char* error, size_t error_size) {
    if (!header || !size) return fail(error, error_size, "Invalid authentication buffer");
    header[0] = '\0';
    if (!nightscout_validate_config(config, error, error_size)) return false;
    if (!config.auth_mode) return true;
    if (config.auth_mode == 1) {
        const size_t len = strlen(config.credential);
        if (len >= size) return fail(error, error_size, "Nightscout token is too long");
        memcpy(header, config.credential, len + 1);
        return true;
    }
    uint8_t digest[20];
    if (size < 41 || !hash || !hash(config.credential, strlen(config.credential), digest))
        return fail(error, error_size, "Unable to prepare Nightscout API secret");
    for (size_t i = 0; i < sizeof(digest); ++i) snprintf(header + i * 2, 3, "%02x", digest[i]);
    return true;
}

bool nightscout_parse(const char* json, size_t size, uint32_t now_epoch,
                      NightscoutResult& result) {
    result = {};
    result.trend = TREND_UNKNOWN;
    if (now_epoch < NIGHTSCOUT_MIN_EPOCH)
        return fail(result.error, sizeof(result.error), "Waiting for clock synchronization");
    if (!json || !size || size > NIGHTSCOUT_MAX_BODY_BYTES)
        return fail(result.error, sizeof(result.error), "Empty or oversized Nightscout response");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, size, DeserializationOption::NestingLimit(8));
    if (err || !doc.is<JsonArray>())
        return fail(result.error, sizeof(result.error), "Invalid Nightscout entries response");
    JsonArray array = doc.as<JsonArray>();
    if (!array.size()) return fail(result.error, sizeof(result.error), "Nightscout has no glucose readings");
    // Refuse an endpoint ignoring the requested bound before scanning its data.
    if (array.size() > 3)
        return fail(result.error, sizeof(result.error), "Nightscout returned too many readings");
    Entry entries[3];
    size_t count = 0;
    for (JsonVariant item : array) {
        if (!item.is<JsonObject>()) continue;
        const char* type = item["type"] | "sgv";
        if (strcmp(type, "sgv")) continue;
        if (!item["sgv"].is<int>() || !item["date"].is<uint64_t>()) continue;
        const int glucose = item["sgv"].as<int>();
        const uint64_t date_ms = item["date"].as<uint64_t>();
        const uint64_t seconds = date_ms / 1000ULL;
        // Nightscout reserves SGV values below 39 for sensor errors, e.g.
        // 5 = not calibrated. These are not glucose values or delta samples.
        // https://github.com/nightscout/cgm-remote-monitor/blob/v15.0.8/lib/plugins/errorcodes.js
        // https://github.com/nightscout/cgm-remote-monitor/blob/v15.0.8/lib/plugins/bgnow.js
        // 1000 is an upper representation sanity bound, not a clinical target.
        if (glucose < 39 || glucose > 1000 || seconds < NIGHTSCOUT_MIN_EPOCH || seconds > UINT32_MAX) continue;
        if (seconds > now_epoch && seconds - now_epoch > NIGHTSCOUT_MAX_FUTURE_SEC) continue;
        const uint32_t stamp = static_cast<uint32_t>(seconds);
        bool duplicate = false;
        for (size_t i = 0; i < count; ++i) {
            if (entries[i].timestamp != stamp) continue;
            if (entries[i].glucose != glucose)
                return fail(result.error, sizeof(result.error), "Conflicting Nightscout readings at one timestamp");
            duplicate = true;
        }
        if (duplicate) continue;
        entries[count++] = {glucose, stamp, direction(item["direction"] | static_cast<const char*>(nullptr))};
    }
    if (!count)
        return fail(result.error, sizeof(result.error), "No valid dated Nightscout glucose readings");
    for (size_t i = 0; i < count; ++i)
        for (size_t j = i + 1; j < count; ++j)
            if (entries[j].timestamp > entries[i].timestamp) {
                Entry tmp = entries[i]; entries[i] = entries[j]; entries[j] = tmp;
            }
    result.glucose = entries[0].glucose;
    result.timestamp = entries[0].timestamp;
    result.trend = entries[0].trend;
    result.age_sec = now_epoch > result.timestamp ? now_epoch - result.timestamp : 0;
    if (count > 1 && result.timestamp - entries[1].timestamp <= NIGHTSCOUT_MAX_DELTA_GAP_SEC) {
        result.has_previous = true;
        result.previous_glucose = entries[1].glucose;
        result.previous_timestamp = entries[1].timestamp;
    }
    return true;
}
