#include "weather_client.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Fetches run only on the background network task (net_task.cpp). The data
// below is also read from the UI loop (core 1) and the async web server
// task, so struct/string state is guarded by data_mutex and word-sized
// values are volatile (32-bit loads/stores are atomic on ESP32).
static SemaphoreHandle_t data_mutex = NULL;
static SemaphoreHandle_t force_done_sem = NULL;
static volatile bool force_requested = false;
static volatile bool force_result = false;

static WeatherReading current_weather;        // guarded by data_mutex
static unsigned long last_poll_ms = 0;        // network task only
static volatile bool ever_received = false;
static volatile int last_http_code = 0;
static char last_response[256] = "";          // guarded by data_mutex

static void set_last_response(const char* s) {
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    strncpy(last_response, s, sizeof(last_response) - 1);
    last_response[sizeof(last_response) - 1] = '\0';
    xSemaphoreGive(data_mutex);
}

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

// Internal fetch logic, network task only (blocks for seconds)
static bool weather_do_fetch() {
    AppConfig& cfg = config_get();

    if (strlen(cfg.weather_api_key) == 0) {
        set_last_response("No API key configured");
        return false;
    }
    if (strlen(cfg.weather_city) == 0) {
        set_last_response("No location configured");
        return false;
    }
    if (!wifi_is_connected()) {
        set_last_response("WiFi not connected");
        return false;
    }

    char url[300];
    build_weather_url(url, sizeof(url));

    esp_task_wdt_reset();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    if (!http.begin(client, url)) {
        Serial.println("[WEATHER] Failed to begin connection");
        last_http_code = -1;
        set_last_response("Failed to connect");
        return false;
    }

    http.setTimeout(10000);

    int httpCode = http.GET();
    last_http_code = httpCode;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();
        set_last_response(payload.c_str());

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[WEATHER] JSON parse error: %s\n", err.c_str());
            char errbuf[128];
            snprintf(errbuf, sizeof(errbuf), "JSON parse error: %s", err.c_str());
            set_last_response(errbuf);
            return false;
        }

        WeatherReading wx = {};
        wx.temp = doc["main"]["temp"] | 0.0f;
        wx.humidity = doc["main"]["humidity"] | 0;

        const char* desc = doc["weather"][0]["main"] | "Unknown";
        strncpy(wx.description, desc, sizeof(wx.description) - 1);
        wx.description[sizeof(wx.description) - 1] = '\0';

        wx.condition_id = doc["weather"][0]["id"] | 0;
        wx.received_at_ms = millis();
        wx.valid = true;

        xSemaphoreTake(data_mutex, portMAX_DELAY);
        current_weather = wx;
        xSemaphoreGive(data_mutex);
        ever_received = true;

        Serial.printf("[WEATHER] Temp: %.1f%s, %s, Humidity: %d%%\n",
                      wx.temp,
                      cfg.weather_use_f ? "F" : "C",
                      wx.description,
                      wx.humidity);
        return true;
    } else {
        // Capture error response body for debugging
        String body = http.getString();
        Serial.printf("[WEATHER] HTTP error: %d, body: %s\n", httpCode, body.c_str());

        char errbuf[256];
        // Try to extract OWM's error message from JSON
        JsonDocument errDoc;
        if (deserializeJson(errDoc, body) == DeserializationError::Ok) {
            const char* msg = errDoc["message"] | "";
            if (strlen(msg) > 0) {
                snprintf(errbuf, sizeof(errbuf), "HTTP %d: %s", httpCode, msg);
            } else {
                snprintf(errbuf, sizeof(errbuf), "HTTP %d", httpCode);
            }
        } else {
            snprintf(errbuf, sizeof(errbuf), "HTTP %d: %s",
                     httpCode, body.length() > 0 ? body.c_str() : "No response");
        }
        set_last_response(errbuf);
    }

    http.end();
    return false;
}

void weather_init() {
    if (data_mutex == NULL) {
        data_mutex = xSemaphoreCreateMutex();
    }
    if (force_done_sem == NULL) {
        force_done_sem = xSemaphoreCreateBinary();
    }

    memset(&current_weather, 0, sizeof(WeatherReading));
    current_weather.valid = false;
    last_poll_ms = 0;
    ever_received = false;
    last_http_code = 0;
    last_response[0] = '\0';
}

void weather_poll_tick() {
    // Forced fetch (web UI test button) works even when weather is disabled,
    // so users can verify their API key before enabling the screen.
    if (force_requested) {
        force_requested = false;
        last_poll_ms = millis();
        force_result = weather_do_fetch();
        xSemaphoreGive(force_done_sem);
        return;
    }

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

bool weather_force_fetch(unsigned long timeout_ms) {
    // Drain a stale completion from a previously timed-out request
    xSemaphoreTake(force_done_sem, 0);
    force_requested = true;

    if (xSemaphoreTake(force_done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false; // still running; result will be published when done
    }
    return force_result;
}

int weather_get_last_http_code() {
    return last_http_code;
}

void weather_get_last_response(char* out, size_t out_len) {
    if (out == NULL || out_len == 0) return;
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    strncpy(out, last_response, out_len - 1);
    out[out_len - 1] = '\0';
    xSemaphoreGive(data_mutex);
}

WeatherReading weather_get_reading() {
    WeatherReading wx;
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    wx = current_weather;
    xSemaphoreGive(data_mutex);
    return wx;
}

bool weather_has_data() {
    return ever_received && current_weather.valid;
}

void weather_set_mock(float temp, const char* desc, int condition_id) {
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    current_weather.temp = temp;
    strncpy(current_weather.description, desc, sizeof(current_weather.description) - 1);
    current_weather.description[sizeof(current_weather.description) - 1] = '\0';
    current_weather.condition_id = condition_id;
    current_weather.humidity = 50;
    current_weather.received_at_ms = millis();
    current_weather.valid = true;
    xSemaphoreGive(data_mutex);
    ever_received = true;
    Serial.printf("[WEATHER] Mock set: %.0f° %s (id=%d)\n", temp, desc, condition_id);
}
