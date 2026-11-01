#include "ble_manager.h"
#include "weather_client.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <atomic>

// Only a completed snapshot is exposed to renderers and web handlers. The
// HTTPS worker owns its result until the main loop publishes it.
static WeatherSnapshot published;
static portMUX_TYPE weather_mux=portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> weather_paused{false};
static unsigned long last_poll_ms=0;
static WeatherPreFetchCallback pre_fetch_cb=nullptr;
static std::atomic<bool> force_requested{false},force_inflight{false};
static std::atomic<bool> fetch_running{false},fetch_complete{false};
static WeatherSnapshot worker_result;
static AppConfig worker_config;
static bool worker_ok=false,worker_holds_network=false;

WeatherSnapshot weather_snapshot() {
    portENTER_CRITICAL(&weather_mux);auto snapshot=published;portEXIT_CRITICAL(&weather_mux);
    return snapshot;
}
static void publish_weather(const WeatherSnapshot& result) {
    portENTER_CRITICAL(&weather_mux);published=result;portEXIT_CRITICAL(&weather_mux);
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
static void build_weather_url(const AppConfig& cfg, char* url, size_t url_len) {
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
static bool weather_do_fetch(const AppConfig& cfg, WeatherSnapshot& result) {
    auto& current_weather=result.reading;
    auto& last_http_code=result.http_code;
    auto& last_response=result.response;
    auto& ever_received=result.ever_received;
    last_http_code=0;

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
    build_weather_url(cfg, url, sizeof(url));

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

        current_weather.condition_id = doc["weather"][0]["id"] | 0;

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

static void weather_worker(void*) {
    worker_ok=weather_do_fetch(worker_config,worker_result);
    if(worker_holds_network) ble_release_network();
    fetch_complete=true;fetch_running=false;
    vTaskDelete(nullptr);
}

void weather_init() {
    publish_weather(WeatherSnapshot{});
    last_poll_ms=0;force_requested=false;force_inflight=false;
}

void weather_loop() {
    if(fetch_running) return;
    if(fetch_complete.exchange(false)) {
        if(force_inflight) {worker_result.force_ok=worker_ok;++worker_result.force_generation;}
        publish_weather(worker_result);force_inflight=false;
    }
    if (weather_paused) return;
    const AppConfig cfg=config_snapshot();
    bool forced=force_requested;
    if(!forced) {
        if(!cfg.weather_enabled || !strlen(cfg.weather_api_key) || !wifi_is_connected()) return;
        const unsigned long interval_ms=(unsigned long)max(5,cfg.weather_poll_min)*60UL*1000UL;
        if(last_poll_ms!=0 && millis()-last_poll_ms<interval_ms) return;
    }
    const bool needs_network=strlen(cfg.weather_api_key) && strlen(cfg.weather_city) && wifi_is_connected();
    if(needs_network && !ble_acquire_network()) return;
    forced=force_requested.exchange(false);force_inflight=forced;worker_holds_network=needs_network;
    worker_config=cfg;worker_result=weather_snapshot();last_poll_ms=millis();fetch_running=true;
    // Drawing belongs to the engine task, before HTTPS starts on the worker.
    if(needs_network && pre_fetch_cb) pre_fetch_cb();
    if(xTaskCreate(weather_worker,"weather_https",12288,nullptr,1,nullptr)!=pdPASS) {
        if(needs_network) ble_release_network();
        fetch_running=false;
        worker_result.http_code=-1000;
        snprintf(worker_result.response,sizeof(worker_result.response),"Could not start weather request");
        if(forced) {worker_result.force_ok=false;++worker_result.force_generation;}
        publish_weather(worker_result);force_inflight=false;
    }
}

void weather_set_paused(bool paused) {weather_paused=paused;}
bool weather_is_paused() {return weather_paused;}
bool weather_force_fetch() {
    if(weather_paused || force_requested || force_inflight) return false;
    force_requested=true;return true;
}
bool weather_force_pending() {return force_requested || force_inflight;}
bool weather_last_force_ok() {return weather_snapshot().force_ok;}
unsigned long weather_force_generation() {return weather_snapshot().force_generation;}
int weather_get_last_http_code() {return weather_snapshot().http_code;}
WeatherReading weather_get_reading() {return weather_snapshot().reading;}
bool weather_has_data() {const auto snapshot=weather_snapshot();return snapshot.ever_received && snapshot.reading.valid;}

void weather_set_pre_fetch_callback(WeatherPreFetchCallback cb) {
    pre_fetch_cb = cb;
}

void weather_set_mock(float temp, const char* desc, int condition_id) {
    portENTER_CRITICAL(&weather_mux);
    auto& current_weather=published.reading;
    current_weather.temp = temp;
    strncpy(current_weather.description, desc, sizeof(current_weather.description) - 1);
    current_weather.description[sizeof(current_weather.description) - 1] = '\0';
    current_weather.condition_id = condition_id;
    current_weather.humidity = 50;
    current_weather.received_at_ms = millis();
    current_weather.valid = true;
    published.ever_received = true;
    portEXIT_CRITICAL(&weather_mux);
    Serial.printf("[WEATHER] Mock set: %.0f° %s (id=%d)\n", temp, desc, condition_id);
}
