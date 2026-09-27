#ifndef NIGHTSCOUT_SETTINGS_H
#define NIGHTSCOUT_SETTINGS_H

#include "nightscout_logic.h"
#include <ArduinoJson.h>
#include <cstring>
#include <cstdio>

// Apply a partial settings request transactionally. An empty credential means
// preserve; clearing is explicit so a redacted settings form cannot erase it.
inline bool nightscout_apply_settings(JsonObjectConst doc, NightscoutConfig& config,
                                     bool active, char* error, size_t error_size) {
    NightscoutConfig next = config;
    auto fail = [&](const char* message) {
        snprintf(error, error_size, "%s", message);
        return false;
    };
    if (!doc["ns_url"].isUnbound()) {
        if (!doc["ns_url"].is<const char*>() || strlen(doc["ns_url"].as<const char*>()) >= sizeof(next.url))
            return fail("Nightscout URL must be shorter than 256 characters");
        snprintf(next.url, sizeof(next.url), "%s", doc["ns_url"].as<const char*>());
    }
    if (!doc["ns_auth_mode"].isUnbound()) {
        if (!doc["ns_auth_mode"].is<int>() || doc["ns_auth_mode"].as<int>() < 0 || doc["ns_auth_mode"].as<int>() > 2)
            return fail("Invalid Nightscout authentication method");
        next.auth_mode = doc["ns_auth_mode"].as<int>();
    }
    if (!doc["ns_clear_credential"].isUnbound() && !doc["ns_clear_credential"].is<bool>())
        return fail("Invalid clear credential setting");
    if (!doc["ns_credential"].isUnbound() &&
        (!doc["ns_credential"].is<const char*>() || strlen(doc["ns_credential"].as<const char*>()) >= sizeof(next.credential)))
        return fail("Nightscout credential must be shorter than 256 characters");
    const char* supplied = doc["ns_credential"] | "";
    const bool clear = doc["ns_clear_credential"] | false;
    if (clear && supplied[0]) return fail("Choose either replace or remove the Nightscout credential");
    if (next.auth_mode != config.auth_mode || clear) next.credential[0] = '\0';
    if (supplied[0]) snprintf(next.credential, sizeof(next.credential), "%s", supplied);

    // Incomplete credentials can be saved/removed. Fetch validation reports
    // the missing credential; validate all other fields before persistence.
    NightscoutConfig check = next;
    if (!check.credential[0] && check.auth_mode != 0)
        snprintf(check.credential, sizeof(check.credential), "%s", "pending-credential");
    if (!active && !check.url[0]) snprintf(check.url, sizeof(check.url), "%s", "https://unconfigured.invalid");
    if (!nightscout_validate_config(check, error, error_size)) return false;
    config = next;
    return true;
}
#endif
