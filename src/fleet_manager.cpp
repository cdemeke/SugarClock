#include "fleet_manager.h"

#include "config_manager.h"
#include "config_patch.h"
#include "fleet_policy.h"
#include "glucose_engine.h"
#include "http_client.h"
#include "notify_engine.h"
#include "ota_manager.h"
#include "ota_trusted_roots.h"
#include "sensors.h"
#include "time_engine.h"
#include "weather_client.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <time.h>

#ifndef SUGARCLOCK_VERSION
#error "SUGARCLOCK_VERSION must be injected from VERSION"
#endif
#ifndef SUGARCLOCK_HARDWARE_ID
#define SUGARCLOCK_HARDWARE_ID "ulanzi-tc001-esp32-4mb"
#endif
#ifndef SUGARCLOCK_FLEET_BASE_URL
#define SUGARCLOCK_FLEET_BASE_URL "https://fleet.sugarclock.com"
#endif
#ifndef SUGARCLOCK_FLEET_ALLOW_INSECURE
#define SUGARCLOCK_FLEET_ALLOW_INSECURE 0
#endif

static const char* FLEET_NAMESPACE = "sugarfleet";
static const uint32_t INITIAL_DELAY_MS = 30000;
static const uint32_t MIN_CHECKIN_SECONDS = 90;
static const uint32_t MAX_CHECKIN_SECONDS = 150;

static char installation_id[37];
static char credential[44];
static char channel[16] = "stable";
static bool registered = false;
static volatile bool worker_running = false;
static bool restart_requested = false;
static uint32_t next_attempt_ms = 0;
static unsigned failure_count = 0;

static char pending_ota_command[37];
static char pending_ota_version[24];

static uint8_t maintenance_days = 0x7f;
static uint16_t maintenance_start = 180;
static uint16_t maintenance_end = 240;
static bool maintenance_automatic = true;

static void copy_text(char* destination, size_t size, const char* source) {
    if (!destination || size == 0) return;
    snprintf(destination, size, "%s", source ? source : "");
}

static void make_uuid(char* output) {
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    snprintf(output, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

static void make_credential(char* output) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint8_t bytes[32];
    esp_fill_random(bytes, sizeof(bytes));
    size_t source = 0;
    size_t target = 0;
    while (source + 3 <= sizeof(bytes)) {
        uint32_t value = (static_cast<uint32_t>(bytes[source]) << 16) |
                         (static_cast<uint32_t>(bytes[source + 1]) << 8) |
                         bytes[source + 2];
        output[target++] = alphabet[(value >> 18) & 63];
        output[target++] = alphabet[(value >> 12) & 63];
        output[target++] = alphabet[(value >> 6) & 63];
        output[target++] = alphabet[value & 63];
        source += 3;
    }
    uint32_t value = static_cast<uint32_t>(bytes[source]) << 16 |
                     static_cast<uint32_t>(bytes[source + 1]) << 8;
    output[target++] = alphabet[(value >> 18) & 63];
    output[target++] = alphabet[(value >> 12) & 63];
    output[target++] = alphabet[(value >> 6) & 63];
    output[target] = '\0';
}

static void load_identity() {
    Preferences prefs;
    if (!prefs.begin(FLEET_NAMESPACE, false)) return;
    prefs.getString("id", installation_id, sizeof(installation_id));
    prefs.getString("credential", credential, sizeof(credential));
    prefs.getString("channel", channel, sizeof(channel));
    registered = prefs.getBool("registered", false);
    prefs.getString("ota_cmd", pending_ota_command, sizeof(pending_ota_command));
    prefs.getString("ota_version", pending_ota_version, sizeof(pending_ota_version));
    maintenance_days = prefs.getUChar("window_days", 0x7f);
    maintenance_start = prefs.getUShort("window_start", 180);
    maintenance_end = prefs.getUShort("window_end", 240);
    maintenance_automatic = prefs.getBool("window_auto", true);

    bool changed = false;
    if (strlen(installation_id) != 36) {
        make_uuid(installation_id);
        prefs.putString("id", installation_id);
        registered = false;
        changed = true;
    }
    if (strlen(credential) != 43) {
        make_credential(credential);
        prefs.putString("credential", credential);
        registered = false;
        changed = true;
    }
    if (strcmp(channel, "stable") != 0 && strcmp(channel, "preview") != 0) {
        copy_text(channel, sizeof(channel), "stable");
        prefs.putString("channel", channel);
    }
    if (changed) prefs.putBool("registered", false);
    prefs.end();
}

static bool begin_request(HTTPClient& http, WiFiClientSecure& secure, WiFiClient& plain,
                          const String& url) {
    if (url.startsWith("https://")) {
        secure.setCACert(OTA_TRUSTED_ROOTS_PEM);
        secure.setHandshakeTimeout(5);
        return http.begin(secure, url);
    }
#if SUGARCLOCK_FLEET_ALLOW_INSECURE
    if (url.startsWith("http://")) return http.begin(plain, url);
#endif
    return false;
}

static int post_json_to_base(const char* base_url, const char* path,
                             const String& payload, String& response) {
    String url = String(base_url) + path;
    WiFiClientSecure secure;
    WiFiClient plain;
    HTTPClient http;
    if (!begin_request(http, secure, plain, url)) return -1;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.setUserAgent(String("SugarClock/") + SUGARCLOCK_VERSION);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + credential);
    int code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(payload.c_str())),
                         payload.length());
    if (code > 0) response = http.getString();
    http.end();
    return code;
}

static int post_json(const char* path, const String& payload, String& response) {
    return post_json_to_base(SUGARCLOCK_FLEET_BASE_URL, path, payload, response);
}

static bool post_result(const char* command_id, const char* status,
                        const char* reason = nullptr, const char* firmware_version = nullptr) {
    JsonDocument doc;
    doc["installation_id"] = installation_id;
    doc["status"] = status;
    if (reason && *reason) doc["reason"] = reason;
    if (firmware_version && *firmware_version) doc["firmware_version"] = firmware_version;
    String payload;
    serializeJson(doc, payload);
    String response;
    String path = String("/device/v1/commands/") + command_id + "/result";
    int code = post_json(path.c_str(), payload, response);
    return code == 200;
}

static void save_pending_ota(const char* command_id, const char* version) {
    copy_text(pending_ota_command, sizeof(pending_ota_command), command_id);
    copy_text(pending_ota_version, sizeof(pending_ota_version), version);
    Preferences prefs;
    if (prefs.begin(FLEET_NAMESPACE, false)) {
        prefs.putString("ota_cmd", pending_ota_command);
        prefs.putString("ota_version", pending_ota_version);
        prefs.end();
    }
}

static void clear_pending_ota() {
    pending_ota_command[0] = '\0';
    pending_ota_version[0] = '\0';
    Preferences prefs;
    if (prefs.begin(FLEET_NAMESPACE, false)) {
        prefs.remove("ota_cmd");
        prefs.remove("ota_version");
        prefs.end();
    }
}

static bool report_pending_ota() {
    if (!pending_ota_command[0]) return true;
    if (strcmp(pending_ota_version, SUGARCLOCK_VERSION) == 0) {
        if (post_result(pending_ota_command, "succeeded", nullptr, SUGARCLOCK_VERSION)) {
            clear_pending_ota();
            return true;
        }
        return false;
    }
    OtaStatusSnapshot ota = {};
    ota_get_status(ota);
    if (ota.state == OTA_ERROR && ota.last_error[0]) {
        if (post_result(pending_ota_command, "failed", ota.last_error)) clear_pending_ota();
    } else if (ota.state == OTA_DEFERRED && ota.safety_reason[0]) {
        if (post_result(pending_ota_command, "deferred", ota.safety_reason)) clear_pending_ota();
    }
    return true;
}

static bool register_device(uint32_t& next_seconds) {
    JsonDocument doc;
    doc["installation_id"] = installation_id;
    doc["hardware"] = SUGARCLOCK_HARDWARE_ID;
    doc["firmware_version"] = SUGARCLOCK_VERSION;
    doc["timezone"] = config_get().timezone;
    doc["management_protocol"] = 1;
    String payload;
    serializeJson(doc, payload);
    String response;
    int code = post_json("/device/v1/register", payload, response);
    if (code != 200 && code != 201) return false;

    JsonDocument result;
    if (deserializeJson(result, response) || !result["status"].is<const char*>()) return false;
    next_seconds = result["next_checkin_seconds"] | 120;
    registered = true;
    Preferences prefs;
    if (prefs.begin(FLEET_NAMESPACE, false)) {
        prefs.putBool("registered", true);
        prefs.end();
    }
    Serial.printf("[FLEET] Registered installation %s\n", installation_id);
    return true;
}

static const char* heap_bucket() {
    uint32_t heap = ESP.getFreeHeap();
    if (heap >= 100000) return "100k_plus";
    if (heap >= 75000) return "75k_plus";
    if (heap >= 50000) return "50k_plus";
    return "under_50k";
}

static const char* signal_bucket() {
    int rssi = wifi_get_rssi();
    if (rssi >= -55) return "excellent";
    if (rssi >= -67) return "good";
    if (rssi >= -75) return "fair";
    return "poor";
}

static bool minute_in_window() {
    int day = time_get_weekday();
    int minute = time_get_hour() * 60 + time_get_minute();
    if (maintenance_start == maintenance_end) return (maintenance_days & (1U << day)) != 0;
    if (maintenance_start < maintenance_end) {
        return (maintenance_days & (1U << day)) &&
               minute >= maintenance_start && minute < maintenance_end;
    }
    if (minute >= maintenance_start) return (maintenance_days & (1U << day)) != 0;
    int previous_day = (day + 6) % 7;
    return minute < maintenance_end && (maintenance_days & (1U << previous_day));
}

static bool save_maintenance(JsonObjectConst payload) {
    const char* timezone = payload["timezone"] | "";
    const char* start = payload["start"] | "";
    const char* end = payload["end"] | "";
    if (strlen(timezone) == 0 || strlen(timezone) >= sizeof(config_get().timezone) ||
        strlen(start) != 5 || strlen(end) != 5 || start[2] != ':' || end[2] != ':' ||
        !payload["days"].is<JsonArrayConst>() || !payload["automatic_install"].is<bool>()) return false;
    int start_hour = (start[0] - '0') * 10 + start[1] - '0';
    int start_minute = (start[3] - '0') * 10 + start[4] - '0';
    int end_hour = (end[0] - '0') * 10 + end[1] - '0';
    int end_minute = (end[3] - '0') * 10 + end[4] - '0';
    if (start_hour > 23 || end_hour > 23 || start_minute > 59 || end_minute > 59) return false;
    uint8_t days = 0;
    for (JsonVariantConst value : payload["days"].as<JsonArrayConst>()) {
        if (!value.is<int>() || value.as<int>() < 0 || value.as<int>() > 6) return false;
        days |= 1U << value.as<int>();
    }
    if (!days) return false;
    maintenance_days = days;
    maintenance_start = start_hour * 60 + start_minute;
    maintenance_end = end_hour * 60 + end_minute;
    maintenance_automatic = payload["automatic_install"].as<bool>();
    ConfigGuard guard;
    AppConfig& cfg = config_get();
    copy_text(cfg.timezone, sizeof(cfg.timezone), timezone);
    cfg.auto_update_enabled = maintenance_automatic;
    cfg.auto_update_hour = start_hour;
    config_save();
    Preferences prefs;
    if (prefs.begin(FLEET_NAMESPACE, false)) {
        prefs.putUChar("window_days", maintenance_days);
        prefs.putUShort("window_start", maintenance_start);
        prefs.putUShort("window_end", maintenance_end);
        prefs.putBool("window_auto", maintenance_automatic);
        prefs.end();
    }
    return true;
}

static bool apply_config_patch(JsonObjectConst changes) {
    for (JsonPairConst pair : changes) {
        const char* name = pair.key().c_str();
        JsonVariantConst value = pair.value();
        bool valid =
            (strcmp(name, "brightness") == 0 && value.is<int>() && value.as<int>() >= 0 && value.as<int>() <= 255) ||
            (strcmp(name, "auto_brightness") == 0 && value.is<bool>()) ||
            (strcmp(name, "show_delta") == 0 && value.is<bool>()) ||
            (strcmp(name, "use_mmol") == 0 && value.is<bool>()) ||
            (strcmp(name, "time_display_enabled") == 0 && value.is<bool>()) ||
            (strcmp(name, "default_mode") == 0 && value.is<int>() && value.as<int>() >= 0 && value.as<int>() <= 3) ||
            (strcmp(name, "ambient_enabled") == 0 && value.is<bool>()) ||
            (strcmp(name, "ambient_creature") == 0 && value.is<int>() && value.as<int>() >= 0 && value.as<int>() <= 1) ||
            (strcmp(name, "ambient_seasonal") == 0 && value.is<bool>()) ||
            (strcmp(name, "notify_enabled") == 0 && value.is<bool>()) ||
            (strcmp(name, "auto_cycle_enabled") == 0 && value.is<bool>()) ||
            (strcmp(name, "auto_cycle_sec") == 0 && value.is<int>() && value.as<int>() >= 3 && value.as<int>() <= 300);
        if (!valid) return false;
    }
    ConfigGuard guard;
    AppConfig candidate=config_get();
    if(config_patch(candidate,changes)) return false;
    config_get()=candidate;
    return config_save();
}

static bool handle_command(JsonObjectConst command) {
    const char* id = command["id"] | "";
    const char* type = command["type"] | "";
    JsonObjectConst payload = command["payload"].as<JsonObjectConst>();
    if (strlen(id) != 36 || !*type || payload.isNull()) return false;
    time_t now = time(nullptr);
    if (command["expires_at"].is<long>() && command["expires_at"].as<long>() <= now) {
        post_result(id, "failed", "command_expired");
        return true;
    }

    if (strcmp(type, "notify") == 0) {
        const char* message = payload["message"] | "";
        int duration = payload["duration_seconds"] | 60;
        if (!*message || strlen(message) >= 64 || duration < 5 || duration > 120)
            return post_result(id, "failed", "invalid_payload");
        notify_push(message, duration, false);
        return post_result(id, "succeeded");
    }
    if (strcmp(type, "set_channel") == 0) {
        const char* value = payload["channel"] | "";
        if (strcmp(value, "stable") != 0 && strcmp(value, "preview") != 0)
            return post_result(id, "failed", "invalid_channel");
        copy_text(channel, sizeof(channel), value);
        Preferences prefs;
        if (prefs.begin(FLEET_NAMESPACE, false)) {
            prefs.putString("channel", channel);
            prefs.end();
        }
        return post_result(id, "succeeded");
    }
    if (strcmp(type, "set_maintenance_window") == 0) {
        bool ok = save_maintenance(payload);
        return post_result(id, ok ? "succeeded" : "failed", ok ? nullptr : "invalid_payload");
    }
    if (strcmp(type, "config_patch") == 0) {
        bool ok = payload["changes"].is<JsonObjectConst>() &&
                  apply_config_patch(payload["changes"].as<JsonObjectConst>());
        return post_result(id, ok ? "succeeded" : "failed", ok ? nullptr : "unsupported_config_field");
    }
    if (strcmp(type, "ota_pause") == 0 && payload["paused"].is<bool>()) {
        { ConfigGuard guard;
        config_get().auto_update_enabled = !payload["paused"].as<bool>();
        config_save(); }
        return post_result(id, "succeeded");
    }
    if (strcmp(type, "ota_check") == 0) {
        if (!post_result(id, "accepted")) return false;
        OtaRequestResult result = ota_request_check();
        if (result == OTA_REQUEST_QUEUED) return true;
        return post_result(id, "failed", "ota_busy");
    }
    if (strcmp(type, "ota_install") == 0) {
        bool override_window = payload["override_window"] | false;
        if (!override_window && !minute_in_window()) return post_result(id, "deferred", "outside_maintenance_window");
        const char* manifest_url = payload["manifest_url"] | "";
        const char* version = payload["version"] | "";
        const char* release_channel = payload["channel"] | "";
        const char* sha256 = payload["sha256"] | "";
        if (!post_result(id, "accepted")) return false;
        save_pending_ota(id, version);
        OtaRequestResult result = ota_request_managed_install(manifest_url, version, release_channel, sha256);
        if (result == OTA_REQUEST_QUEUED) return true;
        bool posted = post_result(id, "failed", "ota_busy");
        if (posted) clear_pending_ota();
        return posted;
    }
    if (strcmp(type, "restart") == 0) {
        bool override_window = payload["override_window"] | false;
        if (!override_window && !minute_in_window()) return post_result(id, "deferred", "outside_maintenance_window");
        bool posted = post_result(id, "succeeded");
        if (posted) restart_requested = true;
        return posted;
    }
    return post_result(id, "failed", "unsupported_command");
}

static bool check_in(uint32_t& next_seconds) {
    OtaStatusSnapshot ota = {};
    ota_get_status(ota);
    JsonDocument doc;
    doc["installation_id"] = installation_id;
    doc["firmware_version"] = SUGARCLOCK_VERSION;
    doc["running_partition"] = ota.running_partition;
    doc["boot_partition"] = ota.boot_partition;
    doc["channel"] = channel;
    doc["timezone"] = config_get().timezone;
    doc["uptime_seconds"] = time_get_uptime_sec();
    doc["free_heap_bucket"] = heap_bucket();
    doc["wifi_signal_bucket"] = signal_bucket();
    int battery = sensors_get_battery_percent();
    if (battery >= 0) doc["battery_percent"] = battery;
    JsonObject window = doc["maintenance_window"].to<JsonObject>();
    window["timezone"] = config_get().timezone;
    JsonArray days = window["days"].to<JsonArray>();
    for (int day = 0; day < 7; ++day) if (maintenance_days & (1U << day)) days.add(day);
    char start[6];
    char end[6];
    snprintf(start, sizeof(start), "%02u:%02u", maintenance_start / 60, maintenance_start % 60);
    snprintf(end, sizeof(end), "%02u:%02u", maintenance_end / 60, maintenance_end % 60);
    window["start"] = start;
    window["end"] = end;
    window["automatic_install"] = maintenance_automatic;
    doc["health_codes"].to<JsonArray>();

    String payload;
    serializeJson(doc, payload);
    String response;
    int code = post_json("/device/v1/check-in", payload, response);
    if (code == 401 || code == 404) {
        registered = false;
        Preferences prefs;
        if (prefs.begin(FLEET_NAMESPACE, false)) {
            prefs.putBool("registered", false);
            prefs.end();
        }
        return false;
    }
    if (code != 200) return false;
    JsonDocument result;
    if (deserializeJson(result, response) || !result["commands"].is<JsonArrayConst>()) return false;
    next_seconds = result["next_checkin_seconds"] | 120;
    for (JsonObjectConst command : result["commands"].as<JsonArrayConst>()) {
        handle_command(command);
        if (restart_requested) break;
    }
    return true;
}

static void fleet_worker(void*) {
    uint32_t next_seconds = 120;
    bool ok = report_pending_ota();
    if (ok && !registered) ok = register_device(next_seconds);
    if (ok && registered) ok = check_in(next_seconds);
    if (ok) {
        failure_count = 0;
        next_seconds = constrain(next_seconds, MIN_CHECKIN_SECONDS, MAX_CHECKIN_SECONDS);
        int jitter = static_cast<int>(esp_random() % 31U) - 15;
        next_attempt_ms = millis() + (next_seconds + jitter) * 1000UL;
    } else {
        ++failure_count;
        uint32_t delay_ms = fleet_retry_delay_ms(failure_count);
        next_attempt_ms = millis() + delay_ms;
        if (fleet_circuit_is_open(failure_count)) {
            Serial.printf("[FLEET] Endpoint circuit open; probing again in %lu minutes\n",
                          static_cast<unsigned long>(delay_ms / 60000UL));
        } else {
            Serial.printf("[FLEET] Check-in failed; retry %u in %lu seconds\n", failure_count,
                          static_cast<unsigned long>(delay_ms / 1000UL));
        }
    }
    worker_running = false;
    if (!ota_is_busy()) {
        http_set_paused(false);
        weather_set_paused(false);
    }
    if (restart_requested) {
        delay(500);
        ESP.restart();
    }
    vTaskDelete(nullptr);
}

void fleet_init() {
    memset(installation_id, 0, sizeof(installation_id));
    memset(credential, 0, sizeof(credential));
    memset(pending_ota_command, 0, sizeof(pending_ota_command));
    memset(pending_ota_version, 0, sizeof(pending_ota_version));
    load_identity();
    next_attempt_ms = millis() + INITIAL_DELAY_MS;
    Serial.printf("[FLEET] Ready: %s via %s\n", installation_id, SUGARCLOCK_FLEET_BASE_URL);
}

void fleet_loop() {
    if (worker_running || http_is_fetching() || ota_is_busy() || !wifi_is_connected() || wifi_is_ap_mode() ||
        !time_is_available() || static_cast<int32_t>(millis() - next_attempt_ms) < 0) return;
    worker_running = true;
    // The ESP32 cannot reliably hold simultaneous TLS handshakes. This bounded
    // attempt runs after core traffic and pauses only future requests. Failed
    // attempts open the circuit, so core traffic is not repeatedly suspended.
    http_set_paused(true);
    weather_set_paused(true);
    if (xTaskCreate(fleet_worker, "fleet", 14336, nullptr, 1, nullptr) != pdPASS) {
        worker_running = false;
        http_set_paused(false);
        weather_set_paused(false);
        next_attempt_ms = millis() + 30000;
        Serial.println("[FLEET] Task creation failed");
    }
}
