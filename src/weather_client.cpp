#include "weather_client.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>

static WeatherReading current_weather;
static unsigned long last_poll_ms = 0;
static bool ever_received = false;
static int last_http_code = 0;
static char last_response[256] = "";

// Detect whether the location string looks like a zip/postal code.
// Returns true for patterns like "90210", "90210,US", "SW1A 1AA,GB"
// Returns false for city patterns like "London,GB", "New York,US"
static bool is_zip_code(const char* loc) {
    if (!loc || strlen(loc) == 0) return false;

    // If first character is a digit, treat as zip code
    // This covers: "90210", "90210,US", "10001", "EC1A,GB", etc.
    if (loc[0] >= '0' && loc[0] <= '9') return true;

    // Check for UK-style postcodes that start with letters: "SW1A 1AA,GB"
    // These have a digit within the first 3 chars and no comma before it
    // Simple heuristic: if there's a space before any comma, and digits mixed in, it's a postcode
    const char* comma = strchr(loc, ',');
    const char* space = strchr(loc, ' ');
    if (space && (!comma || space < comma)) {
        // Has a space before comma (or no comma) — check if digits are present
        for (const char* p = loc; p < (comma ? comma : loc + strlen(loc)); p++) {
            if (*p >= '0' && *p <= '9') return true;
        }
    }

    return false;
}

// Build the weather API URL with auto-detected location type
static void build_weather_url(char* url, size_t url_len) {
    AppConfig& cfg = config_get();
    const char* units = cfg.weather_use_f ? "imperial" : "metric";

    if (is_zip_code(cfg.weather_city)) {
        // Zip/postal code — use zip= parameter
        // If no country code provided, default to US
        if (strchr(cfg.weather_city, ',')) {
            snprintf(url, url_len,
                     "https://api.openweathermap.org/data/2.5/weather?zip=%s&appid=%s&units=%s",
                     cfg.weather_city, cfg.weather_api_key, units);
        } else {
            snprintf(url, url_len,
                     "https://api.openweathermap.org/data/2.5/weather?zip=%s,US&appid=%s&units=%s",
                     cfg.weather_city, cfg.weather_api_key, units);
        }
        Serial.printf("[WEATHER] Using zip code: %s\n", cfg.weather_city);
    } else {
        // City name — use q= parameter
        snprintf(url, url_len,
                 "https://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s",
                 cfg.weather_city, cfg.weather_api_key, units);
        Serial.printf("[WEATHER] Using city: %s\n", cfg.weather_city);
    }
}

// Internal fetch logic (shared by loop and force_fetch)
static bool weather_do_fetch() {
    AppConfig& cfg = config_get();

    if (strlen(cfg.weather_api_key) == 0) {
        strncpy(last_response, "No API key configured", sizeof(last_response) - 1);
        return false;
    }
    if (strlen(cfg.weather_city) == 0) {
        strncpy(last_response, "No location configured", sizeof(last_response) - 1);
        return false;
    }
    if (!wifi_is_connected()) {
        strncpy(last_response, "WiFi not connected", sizeof(last_response) - 1);
        return false;
    }

    char url[300];
    build_weather_url(url, sizeof(url));

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    if (!http.begin(client, url)) {
        Serial.println("[WEATHER] Failed to begin connection");
        last_http_code = -1;
        strncpy(last_response, "Failed to connect", sizeof(last_response) - 1);
        return false;
    }

    http.setTimeout(10000);
    int httpCode = http.GET();
    last_http_code = httpCode;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        strncpy(last_response, payload.c_str(), sizeof(last_response) - 1);
        last_response[sizeof(last_response) - 1] = '\0';

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[WEATHER] JSON parse error: %s\n", err.c_str());
            snprintf(last_response, sizeof(last_response), "JSON parse error: %s", err.c_str());
            http.end();
            return false;
        }

        current_weather.temp = doc["main"]["temp"] | 0.0f;
        current_weather.humidity = doc["main"]["humidity"] | 0;

        const char* desc = doc["weather"][0]["main"] | "Unknown";
        strncpy(current_weather.description, desc, sizeof(current_weather.description) - 1);
        current_weather.description[sizeof(current_weather.description) - 1] = '\0';

        current_weather.received_at_ms = millis();
        current_weather.valid = true;
        ever_received = true;

        Serial.printf("[WEATHER] Temp: %.1f%s, %s, Humidity: %d%%\n",
                      current_weather.temp,
                      cfg.weather_use_f ? "F" : "C",
                      current_weather.description,
                      current_weather.humidity);
        http.end();
        return true;
    } else {
        // Capture error response body for debugging
        String body = http.getString();
        Serial.printf("[WEATHER] HTTP error: %d, body: %s\n", httpCode, body.c_str());

        // Try to extract OWM's error message from JSON
        JsonDocument errDoc;
        if (deserializeJson(errDoc, body) == DeserializationError::Ok) {
            const char* msg = errDoc["message"] | "";
            if (strlen(msg) > 0) {
                snprintf(last_response, sizeof(last_response), "HTTP %d: %s", httpCode, msg);
            } else {
                snprintf(last_response, sizeof(last_response), "HTTP %d", httpCode);
            }
        } else {
            snprintf(last_response, sizeof(last_response), "HTTP %d: %s",
                     httpCode, body.length() > 0 ? body.c_str() : "No response");
        }
    }

    http.end();
    return false;
}

void weather_init() {
    memset(&current_weather, 0, sizeof(WeatherReading));
    current_weather.valid = false;
    last_poll_ms = 0;
    ever_received = false;
    last_http_code = 0;
    last_response[0] = '\0';
}

void weather_loop() {
    AppConfig& cfg = config_get();

    if (!cfg.weather_enabled) return;
    if (strlen(cfg.weather_api_key) == 0) return;
    if (!wifi_is_connected()) return;

    unsigned long interval_ms = (unsigned long)max(5, cfg.weather_poll_min) * 60UL * 1000UL;

    if (last_poll_ms != 0 && (millis() - last_poll_ms < interval_ms)) {
        return;
    }

    last_poll_ms = millis();
    weather_do_fetch();
}

bool weather_force_fetch() {
    last_poll_ms = millis();
    return weather_do_fetch();
}

int weather_get_last_http_code() {
    return last_http_code;
}

const char* weather_get_last_response() {
    return last_response;
}

const WeatherReading& weather_get_reading() {
    return current_weather;
}

bool weather_has_data() {
    return ever_received && current_weather.valid;
}
