#include "wifi_manager.h"
#include "config_manager.h"
#include <WiFi.h>
#include <Arduino.h>

#define WIFI_CONNECT_TIMEOUT_MS   15000
#define WIFI_RETRY_INTERVAL_MS    30000

static char ip_buf[16] = "0.0.0.0";
static char ap_ip_buf[16] = "0.0.0.0";
static const char* status_str = "IDLE";
static unsigned long last_attempt_ms = 0;
static bool connecting = false;
static bool was_connected = false;
static bool ap_mode = false;

void wifi_init() {
    AppConfig& cfg = config_get();

    if (!config_has_wifi()) {
        Serial.println("[WIFI] No WiFi credentials — starting AP mode");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("SugarClock-Setup");
        ap_mode = true;
        IPAddress apIp = WiFi.softAPIP();
        snprintf(ap_ip_buf, sizeof(ap_ip_buf), "%d.%d.%d.%d", apIp[0], apIp[1], apIp[2], apIp[3]);
        Serial.printf("[WIFI] AP started: SSID=SugarClock-Setup  IP=%s\n", ap_ip_buf);
        status_str = "AP MODE";
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    Serial.printf("[WIFI] Connecting to '%s'...\n", cfg.wifi_ssid);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
    connecting = true;
    last_attempt_ms = millis();
    status_str = "CONNECTING";
}

void wifi_loop() {
    if (ap_mode) return;
    if (!config_has_wifi()) return;

    if (WiFi.status() == WL_CONNECTED) {
        if (!was_connected) {
            // Just connected
            IPAddress ip = WiFi.localIP();
            snprintf(ip_buf, sizeof(ip_buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            Serial.printf("[WIFI] Connected! IP: %s, RSSI: %d dBm\n", ip_buf, WiFi.RSSI());
            was_connected = true;
            connecting = false;
            status_str = "CONNECTED";
        }
        return;
    }

    // Not connected
    if (was_connected) {
        Serial.println("[WIFI] Connection lost, will auto-reconnect");
        was_connected = false;
        status_str = "RECONNECTING";
    }

    // Check connection timeout
    if (connecting && (millis() - last_attempt_ms > WIFI_CONNECT_TIMEOUT_MS)) {
        Serial.println("[WIFI] Connection timeout");
        connecting = false;
        status_str = "TIMEOUT";
    }

    // Retry logic (non-blocking)
    if (!connecting && (millis() - last_attempt_ms > WIFI_RETRY_INTERVAL_MS)) {
        AppConfig& cfg = config_get();
        Serial.printf("[WIFI] Retrying connection to '%s'...\n", cfg.wifi_ssid);
        WiFi.disconnect();
        WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
        connecting = true;
        last_attempt_ms = millis();
        status_str = "CONNECTING";
    }
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

const char* wifi_get_ip() {
    return ip_buf;
}

int wifi_get_rssi() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

const char* wifi_get_status() {
    return status_str;
}

bool wifi_is_ap_mode() {
    return ap_mode;
}

const char* wifi_get_ap_ip() {
    return ap_ip_buf;
}
