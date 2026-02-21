#include "web_server.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "glucose_engine.h"
#include "time_engine.h"
#include "sensors.h"
#include "display.h"
#include "weather_client.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>

static AsyncWebServer server(80);
static bool started = false;

// Helper: convert uint32_t RGB to "#RRGGBB" hex string
static String color_to_hex(uint32_t c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    return String(buf);
}

// Helper: parse "#RRGGBB" hex string to uint32_t RGB
static uint32_t hex_to_color(const char* hex) {
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return 0;
    return strtoul(hex + 1, NULL, 16);
}

// GET /api/status
static void handle_status(AsyncWebServerRequest* request) {
    JsonDocument doc;

    const GlucoseReading& r = http_get_reading();
    doc["glucose"] = r.valid ? r.glucose : 0;
    doc["trend"] = r.valid ? TREND_NAMES[r.trend] : "Unknown";
    doc["valid"] = r.valid;
    doc["data_age_sec"] = r.valid ? (millis() - r.received_at_ms) / 1000 : -1;
    doc["state"] = engine_state_name(engine_get_state());
    doc["wifi_connected"] = wifi_is_connected();
    doc["wifi_ip"] = wifi_get_ip();
    doc["wifi_rssi"] = wifi_get_rssi();
    doc["uptime_sec"] = time_get_uptime_sec();
    doc["failure_count"] = http_get_failure_count();
    doc["brightness"] = display_get_brightness();
    doc["message"] = r.message;

    // Delta
    doc["delta"] = http_get_delta();

    // Glucose color info
    AppConfig& cfg = config_get();
    if (r.valid) {
        if (r.glucose < cfg.thresh_urgent_low || r.glucose > cfg.thresh_urgent_high) {
            doc["color"] = "red";
        } else if (r.glucose < cfg.thresh_low || r.glucose > cfg.thresh_high) {
            doc["color"] = "orange";
        } else {
            doc["color"] = "green";
        }
    } else {
        doc["color"] = "gray";
    }

    // Thresholds (for web UI graph)
    JsonObject thresholds = doc["thresholds"].to<JsonObject>();
    thresholds["urgent_low"] = cfg.thresh_urgent_low;
    thresholds["low"] = cfg.thresh_low;
    thresholds["high"] = cfg.thresh_high;
    thresholds["urgent_high"] = cfg.thresh_urgent_high;

    // Weather
    if (weather_has_data()) {
        const WeatherReading& wx = weather_get_reading();
        doc["weather_temp"] = wx.temp;
        doc["weather_desc"] = wx.description;
        doc["weather_humidity"] = wx.humidity;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/config
static void handle_get_config(AsyncWebServerRequest* request) {
    AppConfig& cfg = config_get();
    JsonDocument doc;

    doc["wifi_ssid"] = cfg.wifi_ssid;
    doc["wifi_password"] = ""; // never send password back
    doc["data_source"] = cfg.data_source;
    doc["server_url"] = cfg.server_url;
    doc["auth_token"] = strlen(cfg.auth_token) > 0 ? "****" : "";
    doc["dexcom_username"] = cfg.dexcom_username;
    doc["dexcom_password"] = strlen(cfg.dexcom_password) > 0 ? "****" : "";
    doc["dexcom_us"] = cfg.dexcom_us;
    doc["poll_interval"] = cfg.poll_interval_sec;
    doc["brightness"] = cfg.brightness;
    doc["auto_brightness"] = cfg.auto_brightness;
    doc["show_delta"] = cfg.show_delta;
    doc["use_mmol"] = cfg.use_mmol;
    doc["thresh_urgent_low"] = cfg.thresh_urgent_low;
    doc["thresh_low"] = cfg.thresh_low;
    doc["thresh_high"] = cfg.thresh_high;
    doc["thresh_urgent_high"] = cfg.thresh_urgent_high;
    doc["timezone"] = cfg.timezone;
    doc["use_24h"] = cfg.use_24h;
    doc["default_mode"] = cfg.default_mode;

    // Alerts
    doc["alert_enabled"] = cfg.alert_enabled;
    doc["alert_low"] = cfg.alert_low;
    doc["alert_high"] = cfg.alert_high;
    doc["alert_snooze_min"] = cfg.alert_snooze_min;

    // Theme colors
    doc["color_urgent_low"] = color_to_hex(cfg.color_urgent_low);
    doc["color_low"] = color_to_hex(cfg.color_low);
    doc["color_in_range"] = color_to_hex(cfg.color_in_range);
    doc["color_high"] = color_to_hex(cfg.color_high);
    doc["color_urgent_high"] = color_to_hex(cfg.color_urgent_high);

    // Night mode
    doc["night_mode_enabled"] = cfg.night_mode_enabled;
    doc["night_start_hour"] = cfg.night_start_hour;
    doc["night_end_hour"] = cfg.night_end_hour;
    doc["night_brightness"] = cfg.night_brightness;

    // Stale timeout
    doc["stale_timeout_min"] = cfg.stale_timeout_min;

    // Weather
    doc["weather_enabled"] = cfg.weather_enabled;
    doc["weather_api_key"] = strlen(cfg.weather_api_key) > 0 ? "****" : "";
    doc["weather_city"] = cfg.weather_city;
    doc["weather_use_f"] = cfg.weather_use_f;
    doc["weather_poll_min"] = cfg.weather_poll_min;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/config (JSON body)
static void handle_post_config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (index != 0) return; // only process first chunk

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    AppConfig& cfg = config_get();

    if (doc["wifi_ssid"].is<const char*>()) {
        strncpy(cfg.wifi_ssid, doc["wifi_ssid"] | "", sizeof(cfg.wifi_ssid) - 1);
    }
    if (doc["wifi_password"].is<const char*>()) {
        const char* pw = doc["wifi_password"] | "";
        if (strlen(pw) > 0) { // only update if non-empty
            strncpy(cfg.wifi_password, pw, sizeof(cfg.wifi_password) - 1);
        }
    }
    if (doc["data_source"].is<int>()) {
        cfg.data_source = doc["data_source"].as<int>();
    }
    if (doc["server_url"].is<const char*>()) {
        strncpy(cfg.server_url, doc["server_url"] | "", sizeof(cfg.server_url) - 1);
    }
    if (doc["auth_token"].is<const char*>()) {
        const char* token = doc["auth_token"] | "";
        if (strcmp(token, "****") != 0 && strlen(token) > 0) {
            strncpy(cfg.auth_token, token, sizeof(cfg.auth_token) - 1);
        }
    }
    if (doc["dexcom_username"].is<const char*>()) {
        strncpy(cfg.dexcom_username, doc["dexcom_username"] | "", sizeof(cfg.dexcom_username) - 1);
    }
    if (doc["dexcom_password"].is<const char*>()) {
        const char* dp = doc["dexcom_password"] | "";
        if (strcmp(dp, "****") != 0 && strlen(dp) > 0) {
            strncpy(cfg.dexcom_password, dp, sizeof(cfg.dexcom_password) - 1);
        }
    }
    if (doc["dexcom_us"].is<bool>()) {
        cfg.dexcom_us = doc["dexcom_us"].as<bool>();
    }
    if (doc["poll_interval"].is<int>()) {
        cfg.poll_interval_sec = max(15, doc["poll_interval"].as<int>());
    }
    if (doc["brightness"].is<int>()) {
        cfg.brightness = constrain(doc["brightness"].as<int>(), 1, 255);
    }
    if (doc["auto_brightness"].is<bool>()) {
        cfg.auto_brightness = doc["auto_brightness"].as<bool>();
    }
    if (doc["show_delta"].is<bool>()) {
        cfg.show_delta = doc["show_delta"].as<bool>();
    }
    if (doc["use_mmol"].is<bool>()) {
        cfg.use_mmol = doc["use_mmol"].as<bool>();
    }
    if (doc["thresh_urgent_low"].is<int>()) {
        cfg.thresh_urgent_low = doc["thresh_urgent_low"].as<int>();
    }
    if (doc["thresh_low"].is<int>()) {
        cfg.thresh_low = doc["thresh_low"].as<int>();
    }
    if (doc["thresh_high"].is<int>()) {
        cfg.thresh_high = doc["thresh_high"].as<int>();
    }
    if (doc["thresh_urgent_high"].is<int>()) {
        cfg.thresh_urgent_high = doc["thresh_urgent_high"].as<int>();
    }
    if (doc["timezone"].is<const char*>()) {
        strncpy(cfg.timezone, doc["timezone"] | "", sizeof(cfg.timezone) - 1);
    }
    if (doc["use_24h"].is<bool>()) {
        cfg.use_24h = doc["use_24h"].as<bool>();
    }
    if (doc["default_mode"].is<int>()) {
        cfg.default_mode = doc["default_mode"].as<int>();
    }

    // Alerts
    if (doc["alert_enabled"].is<bool>()) {
        cfg.alert_enabled = doc["alert_enabled"].as<bool>();
    }
    if (doc["alert_low"].is<int>()) {
        cfg.alert_low = doc["alert_low"].as<int>();
    }
    if (doc["alert_high"].is<int>()) {
        cfg.alert_high = doc["alert_high"].as<int>();
    }
    if (doc["alert_snooze_min"].is<int>()) {
        cfg.alert_snooze_min = constrain(doc["alert_snooze_min"].as<int>(), 1, 120);
    }

    // Theme colors
    if (doc["color_urgent_low"].is<const char*>()) {
        cfg.color_urgent_low = hex_to_color(doc["color_urgent_low"] | "#ea4335");
    }
    if (doc["color_low"].is<const char*>()) {
        cfg.color_low = hex_to_color(doc["color_low"] | "#fbbc04");
    }
    if (doc["color_in_range"].is<const char*>()) {
        cfg.color_in_range = hex_to_color(doc["color_in_range"] | "#34a853");
    }
    if (doc["color_high"].is<const char*>()) {
        cfg.color_high = hex_to_color(doc["color_high"] | "#fbbc04");
    }
    if (doc["color_urgent_high"].is<const char*>()) {
        cfg.color_urgent_high = hex_to_color(doc["color_urgent_high"] | "#ea4335");
    }

    // Night mode
    if (doc["night_mode_enabled"].is<bool>()) {
        cfg.night_mode_enabled = doc["night_mode_enabled"].as<bool>();
    }
    if (doc["night_start_hour"].is<int>()) {
        cfg.night_start_hour = constrain(doc["night_start_hour"].as<int>(), 0, 23);
    }
    if (doc["night_end_hour"].is<int>()) {
        cfg.night_end_hour = constrain(doc["night_end_hour"].as<int>(), 0, 23);
    }
    if (doc["night_brightness"].is<int>()) {
        cfg.night_brightness = constrain(doc["night_brightness"].as<int>(), 1, 255);
    }

    // Stale timeout
    if (doc["stale_timeout_min"].is<int>()) {
        cfg.stale_timeout_min = constrain(doc["stale_timeout_min"].as<int>(), 5, 60);
    }

    // Weather
    if (doc["weather_enabled"].is<bool>()) {
        cfg.weather_enabled = doc["weather_enabled"].as<bool>();
    }
    if (doc["weather_api_key"].is<const char*>()) {
        const char* wk = doc["weather_api_key"] | "";
        if (strcmp(wk, "****") != 0 && strlen(wk) > 0) {
            strncpy(cfg.weather_api_key, wk, sizeof(cfg.weather_api_key) - 1);
            cfg.weather_api_key[sizeof(cfg.weather_api_key) - 1] = '\0';
        }
    }
    if (doc["weather_city"].is<const char*>()) {
        strncpy(cfg.weather_city, doc["weather_city"] | "", sizeof(cfg.weather_city) - 1);
        cfg.weather_city[sizeof(cfg.weather_city) - 1] = '\0';
    }
    if (doc["weather_use_f"].is<bool>()) {
        cfg.weather_use_f = doc["weather_use_f"].as<bool>();
    }
    if (doc["weather_poll_min"].is<int>()) {
        cfg.weather_poll_min = constrain(doc["weather_poll_min"].as<int>(), 5, 60);
    }

    config_save();

    // Apply brightness immediately
    if (!cfg.auto_brightness) {
        display_set_brightness(cfg.brightness);
    }

    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// GET /api/debug
static void handle_debug(AsyncWebServerRequest* request) {
    JsonDocument doc;

    doc["last_http_code"] = http_get_last_response_code();
    doc["last_http_body"] = http_get_last_response_body();
    doc["failure_count"] = http_get_failure_count();
    doc["ever_received"] = http_has_ever_received();
    doc["wifi_rssi"] = wifi_get_rssi();
    doc["wifi_status"] = wifi_get_status();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["min_free_heap"] = ESP.getMinFreeHeap();
    doc["largest_free_block"] = ESP.getMaxAllocHeap();
    doc["uptime_sec"] = time_get_uptime_sec();
    doc["display_state"] = engine_state_name(engine_get_state());
    doc["ldr_raw"] = sensors_get_ldr();
    doc["auto_brightness_val"] = sensors_get_auto_brightness();
    doc["battery_voltage"] = sensors_get_battery_voltage();
    doc["battery_percent"] = sensors_get_battery_percent();

    // MAC address
    doc["mac"] = WiFi.macAddress();

    // LittleFS info
    doc["fs_used"] = LittleFS.usedBytes();
    doc["fs_total"] = LittleFS.totalBytes();

    unsigned long age = http_time_since_last_reading();
    doc["data_age_ms"] = (age == ULONG_MAX) ? -1 : (long)age;

    const GlucoseReading& r = http_get_reading();
    if (r.valid) {
        doc["raw_glucose"] = r.glucose;
        doc["raw_trend"] = TREND_NAMES[r.trend];
        doc["raw_message"] = r.message;
        doc["raw_force_mode"] = r.force_mode;
        doc["raw_delta"] = http_get_delta();
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/history
static void handle_history(AsyncWebServerRequest* request) {
    JsonDocument doc;

    GlucoseHistoryEntry entries[GLUCOSE_HISTORY_SIZE];
    int count = http_get_history(entries, GLUCOSE_HISTORY_SIZE);

    JsonArray readings = doc["readings"].to<JsonArray>();
    for (int i = 0; i < count; i++) {
        JsonObject entry = readings.add<JsonObject>();
        entry["glucose"] = entries[i].glucose;
        entry["delta"] = entries[i].delta;
        entry["ts"] = entries[i].timestamp;
    }
    doc["count"] = count;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/restart
static void handle_restart(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
}

// POST /api/factory-reset
static void handle_factory_reset(AsyncWebServerRequest* request) {
    config_reset();
    request->send(200, "application/json", "{\"status\":\"factory reset, restarting\"}");
    delay(500);
    ESP.restart();
}

void webserver_init() {
    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[WEB] LittleFS mount failed");
        return;
    }
    Serial.println("[WEB] LittleFS mounted");

    // Static files from /www/
    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

    // API routes
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/config", HTTP_GET, handle_get_config);
    server.on("/api/debug", HTTP_GET, handle_debug);
    server.on("/api/history", HTTP_GET, handle_history);
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* r) { handle_restart(r); });
    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* r) { handle_factory_reset(r); });

    // POST /api/config with body
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_post_config
    );

    // CORS headers for development
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    Serial.println("[WEB] Routes registered");
}

void webserver_start() {
    if (started) return;
    server.begin();
    started = true;
    Serial.printf("[WEB] Server started at http://%s/\n", wifi_get_ip());
}
