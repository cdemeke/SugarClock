#include "blocks_client.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>

static BlocksReading current_blocks;
static unsigned long last_poll_ms = 0;
static bool ever_received = false;
static int last_http_code = 0;
static char last_response[256] = "";
static BlocksPreFetchCallback pre_fetch_cb = nullptr;

// Internal fetch logic (shared by loop and force_fetch)
static bool blocks_do_fetch() {
    AppConfig& cfg = config_get();

    if (strlen(cfg.blocks_url) == 0) {
        strncpy(last_response, "No URL configured", sizeof(last_response) - 1);
        last_response[sizeof(last_response) - 1] = '\0';
        return false;
    }
    if (!wifi_is_connected()) {
        strncpy(last_response, "WiFi not connected", sizeof(last_response) - 1);
        last_response[sizeof(last_response) - 1] = '\0';
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    if (!http.begin(client, cfg.blocks_url)) {
        Serial.println("[BLOCKS] Failed to begin connection");
        last_http_code = -1;
        strncpy(last_response, "Failed to connect", sizeof(last_response) - 1);
        last_response[sizeof(last_response) - 1] = '\0';
        return false;
    }

    http.setTimeout(10000);

    // Notify engine before the blocking HTTP call so it can render a clean frame.
    if (pre_fetch_cb) pre_fetch_cb();

    int httpCode = http.GET();
    last_http_code = httpCode;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        strncpy(last_response, payload.c_str(), sizeof(last_response) - 1);
        last_response[sizeof(last_response) - 1] = '\0';

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[BLOCKS] JSON parse error: %s\n", err.c_str());
            snprintf(last_response, sizeof(last_response), "JSON parse error: %s", err.c_str());
            http.end();
            return false;  // keep previous reading on transient failure
        }

        if (!(doc["ok"] | false)) {
            Serial.println("[BLOCKS] Response ok=false");
            strncpy(last_response, "Response ok=false", sizeof(last_response) - 1);
            last_response[sizeof(last_response) - 1] = '\0';
            http.end();
            return false;  // keep previous reading
        }

        // Build into a fresh reading, then commit on success.
        BlocksReading reading;
        memset(&reading, 0, sizeof(BlocksReading));
        reading.kid_count = 0;

        JsonArray kids = doc["kids"].as<JsonArray>();
        for (JsonObject kid : kids) {
            if (reading.kid_count >= BLOCKS_MAX_KIDS) break;
            BlocksKid& bk = reading.kids[reading.kid_count];

            const char* name = kid["n"] | "";
            strncpy(bk.name, name, sizeof(bk.name) - 1);
            bk.name[sizeof(bk.name) - 1] = '\0';
            bk.remaining = kid["r"] | 0;
            bk.allocation = kid["a"] | 0;

            reading.kid_count++;
        }

        reading.received_at_ms = millis();
        reading.valid = true;
        current_blocks = reading;
        ever_received = true;

        Serial.printf("[BLOCKS] %d kid(s) received\n", current_blocks.kid_count);
        if (current_blocks.kid_count > 0) {
            Serial.printf("[BLOCKS] First: %s %d/%d\n",
                          current_blocks.kids[0].name,
                          current_blocks.kids[0].remaining,
                          current_blocks.kids[0].allocation);
        }
        http.end();
        return true;
    } else {
        String body = http.getString();
        Serial.printf("[BLOCKS] HTTP error: %d, body: %s\n", httpCode, body.c_str());
        snprintf(last_response, sizeof(last_response), "HTTP %d: %s",
                 httpCode, body.length() > 0 ? body.c_str() : "No response");
    }

    http.end();
    return false;
}

void blocks_init() {
    memset(&current_blocks, 0, sizeof(BlocksReading));
    current_blocks.valid = false;
    last_poll_ms = 0;
    ever_received = false;
    last_http_code = 0;
    last_response[0] = '\0';
}

void blocks_loop() {
    AppConfig& cfg = config_get();

    if (!cfg.blocks_enabled) return;
    if (strlen(cfg.blocks_url) == 0) return;
    if (!wifi_is_connected()) return;

    unsigned long interval_ms = (unsigned long)max(1, cfg.blocks_poll_min) * 60UL * 1000UL;

    if (last_poll_ms != 0 && (millis() - last_poll_ms < interval_ms)) {
        return;
    }

    last_poll_ms = millis();
    blocks_do_fetch();
}

bool blocks_force_fetch() {
    last_poll_ms = millis();
    return blocks_do_fetch();
}

int blocks_get_last_http_code() {
    return last_http_code;
}

const char* blocks_get_last_response() {
    return last_response;
}

const BlocksReading& blocks_get_reading() {
    return current_blocks;
}

bool blocks_has_data() {
    return ever_received && current_blocks.valid;
}

bool blocks_is_stale() {
    if (!blocks_has_data()) return false;
    AppConfig& cfg = config_get();
    unsigned long interval_ms = (unsigned long)max(1, cfg.blocks_poll_min) * 60UL * 1000UL;
    return (millis() - current_blocks.received_at_ms) > (3UL * interval_ms);
}

void blocks_set_pre_fetch_callback(BlocksPreFetchCallback cb) {
    pre_fetch_cb = cb;
}
