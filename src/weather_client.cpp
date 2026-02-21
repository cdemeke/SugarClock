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

void weather_init() {
    memset(&current_weather, 0, sizeof(WeatherReading));
    current_weather.valid = false;
    last_poll_ms = 0;
    ever_received = false;
}

void weather_loop() {
    AppConfig& cfg = config_get();

    // Skip if weather not enabled or no API key
    if (!cfg.weather_enabled) return;
    if (strlen(cfg.weather_api_key) == 0) return;
    if (!wifi_is_connected()) return;

    unsigned long interval_ms = (unsigned long)max(5, cfg.weather_poll_min) * 60UL * 1000UL;

    if (last_poll_ms != 0 && (millis() - last_poll_ms < interval_ms)) {
        return;
    }

    last_poll_ms = millis();

    // Build URL
    const char* units = cfg.weather_use_f ? "imperial" : "metric";
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s",
             cfg.weather_city, cfg.weather_api_key, units);

    Serial.printf("[WEATHER] Fetching: %s\n", cfg.weather_city);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    if (!http.begin(client, url)) {
        Serial.println("[WEATHER] Failed to begin connection");
        return;
    }

    http.setTimeout(10000);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[WEATHER] JSON parse error: %s\n", err.c_str());
            http.end();
            return;
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
    } else {
        Serial.printf("[WEATHER] HTTP error: %d\n", httpCode);
    }

    http.end();
}

const WeatherReading& weather_get_reading() {
    return current_weather;
}

bool weather_has_data() {
    return ever_received && current_weather.valid;
}
