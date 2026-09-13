#include "fleet_manager.h"

#include "config_manager.h"
#include "companion.h"
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
static char pending_ota_channel[16];
static bool pending_install_authorized = false;
static time_t authorization_expires_at = 0;
static bool pending_is_target = false;
static bool pending_from_boot = false;
static char pending_outcome[20];
static char pending_outcome_reason[32];
static FleetManualIntent manual_intent;
static portMUX_TYPE manual_intent_mux = portMUX_INITIALIZER_UNLOCKED;
static bool pending_manual_install = false;

static uint8_t maintenance_days = 0x7f;
static uint16_t maintenance_start = 180;
static uint16_t maintenance_end = 240;
static bool maintenance_automatic = true;
static bool maintenance_custom = false;

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
    prefs.getString("ota_outcome", pending_outcome, sizeof(pending_outcome));
    prefs.getString("ota_reason", pending_outcome_reason, sizeof(pending_outcome_reason));
    prefs.getString("ota_channel", pending_ota_channel, sizeof(pending_ota_channel));
    pending_install_authorized = prefs.getBool("ota_started", false);
    pending_is_target = prefs.getBool("ota_target", false);
    pending_from_boot = pending_ota_command[0] != '\0';
    maintenance_custom = prefs.isKey("window_start");
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

// Bound both decoded JSON and chunked/error responses on the ESP32 heap.
class FleetResponseStream : public Stream {
public:
    explicit FleetResponseStream(String& output) : output_(output) {}
    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t* bytes, size_t size) override {
        if (output_.length() + size > 8192) return 0;
        return output_.concat(reinterpret_cast<const char*>(bytes), size) ? size : 0;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
private:
    String& output_;
};

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
    if (code > 0) {
        FleetResponseStream bounded(response);
        if (http.getSize() > 8192 || http.writeToStream(&bounded) < 0) code = -1;
    }
    http.end();
    return code;
}

static int post_json(const char* path, const String& payload, String& response) {
    return post_json_to_base(SUGARCLOCK_FLEET_BASE_URL, path, payload, response);
}

static bool post_result(const char* command_id, const char* status,
                        const char* reason = nullptr, const char* firmware_version = nullptr, bool target = false) {
    JsonDocument doc;
    doc["installation_id"] = installation_id;
    doc["status"] = status;
    if (reason && *reason) doc["reason"] = reason;
    if (firmware_version && *firmware_version) doc["firmware_version"] = firmware_version;
    String payload;
    serializeJson(doc, payload);
    String response;
    String path = String(target ? "/device/v1/updates/" : "/device/v1/commands/") + command_id + "/result";
    int code = post_json(path.c_str(), payload, response);
    // A target removed by bounded server retention cannot be reconciled; forget
    // its local ACK so a returning clock can accept its new authoritative target.
    return code == 200 || (target && code == 404);
}

static bool save_pending_ota(const char* command_id, const char* version, const char* release_channel) {
    copy_text(pending_ota_command, sizeof(pending_ota_command), command_id);
    copy_text(pending_ota_version, sizeof(pending_ota_version), version);
    copy_text(pending_ota_channel, sizeof(pending_ota_channel), release_channel);
    pending_install_authorized = false;
    authorization_expires_at = 0;
    pending_outcome[0] = '\0';
    pending_outcome_reason[0] = '\0';
    pending_manual_install = false;
    pending_is_target = true;
    pending_from_boot = false;
    Preferences prefs;
    if (!prefs.begin(FLEET_NAMESPACE, false)) return false;
    prefs.remove("ota_outcome");
    prefs.remove("ota_reason");
    bool ok = prefs.putString("ota_cmd", pending_ota_command) &&
              prefs.putString("ota_version", pending_ota_version) &&
              prefs.putString("ota_channel", pending_ota_channel) &&
              prefs.putBool("ota_target", true) && prefs.putBool("ota_started", false);
    prefs.end();
    return ok;
}

static void clear_pending_ota() {
    pending_manual_install = false;
    pending_ota_command[0] = '\0';
    pending_ota_version[0] = '\0';
    Preferences prefs;
    if (prefs.begin(FLEET_NAMESPACE, false)) {
        prefs.remove("ota_cmd");
        prefs.remove("ota_version");
        prefs.remove("ota_target");
        prefs.remove("ota_outcome");
        prefs.remove("ota_reason");
        prefs.remove("ota_started");
        prefs.remove("ota_channel");
        prefs.end();
    }
}

static bool report_pending_ota() {
    if (!pending_ota_command[0]) return true;
    OtaStatusSnapshot ota = {};
    ota_get_status(ota);
    const char* status = nullptr;
    const char* reason = nullptr;
    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    const esp_partition_t* running = esp_ota_get_running_partition();
    bool image_valid = running && esp_ota_get_state_partition(running, &image_state) == ESP_OK &&
                       image_state == ESP_OTA_IMG_VALID;
    FleetBootOutcome boot = fleet_boot_outcome(
        strcmp(pending_ota_version, SUGARCLOCK_VERSION) == 0, image_valid,
        ota.pending_verification, strcmp(ota.last_error, "rollback_detected") == 0,
        pending_from_boot, !pending_is_target || pending_install_authorized);
    if (boot == FLEET_BOOT_VALIDATED) {
        status = pending_is_target ? "boot_validated" : "succeeded";
        if (strcmp(pending_ota_channel, "stable") == 0 || strcmp(pending_ota_channel, "preview") == 0) {
            copy_text(channel, sizeof(channel), pending_ota_channel);
            Preferences prefs;
            if (prefs.begin(FLEET_NAMESPACE, false)) {
                prefs.putString("channel", channel);
                prefs.end();
            }
        }
    }
    else if (boot == FLEET_BOOT_ROLLED_BACK) {
        status = pending_is_target ? "rolled_back" : "failed";
        reason = "rollback_detected";
    } else if (pending_outcome[0]) {
        status = pending_outcome;
        reason = pending_outcome_reason[0] ? pending_outcome_reason :
                 (strcmp(status, "deferred") == 0 ? "local_safety" : "update_failed");
    } else if (boot == FLEET_BOOT_DEFERRED) {
        status = "deferred";
        reason = "interrupted_before_authorization";
    } else if (boot == FLEET_BOOT_INTERRUPTED) {
        status = "failed";
        reason = "interrupted";
    } else if (ota.state == OTA_ERROR) {
        status = "failed";
        reason = "update_failed";
    } else if (ota.state == OTA_DEFERRED) {
        status = "deferred";
        reason = "local_safety";
    }
    if (status && post_result(pending_ota_command, status, reason, SUGARCLOCK_VERSION,
                              pending_is_target)) clear_pending_ota();
    // A lost result must not stop routine reporting; don't replace it with another offer.
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
    next_seconds = result["next_checkin_seconds"] | 300;
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
    if (!maintenance_custom) return time_get_hour() == config_get().auto_update_hour;
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
    maintenance_custom = true;
    maintenance_days = days;
    maintenance_start = start_hour * 60 + start_minute;
    maintenance_end = end_hour * 60 + end_minute;
    maintenance_automatic = payload["automatic_install"].as<bool>();
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
            (strcmp(name, "ambient_use_glucose_colors") == 0 && value.is<bool>()) ||
            (strcmp(name, "ambient_style") == 0 && value.is<int>() && companion_style_valid(value.as<int>())) ||
            (strcmp(name, "ambient_creature") == 0 && value.is<int>() && value.as<int>() >= 0 && value.as<int>() <= 1) ||
            (strcmp(name, "ambient_character") == 0 && value.is<int>() && companion_valid(value.as<int>())) ||
            (strcmp(name, "ambient_enabled") == 0 && value.is<bool>()) ||
            (strcmp(name, "ambient_seasonal") == 0 && value.is<bool>()) ||
            (strcmp(name, "notify_enabled") == 0 && value.is<bool>()) ||
            (strcmp(name, "auto_cycle_enabled") == 0 && value.is<bool>()) ||
            (strcmp(name, "auto_cycle_sec") == 0 && value.is<int>() && value.as<int>() >= 3 && value.as<int>() <= 300);
        if (!valid) return false;
    }
    AppConfig& cfg = config_get();
    for (JsonPairConst pair : changes) {
        const char* name = pair.key().c_str();
        JsonVariantConst value = pair.value();
        if (strcmp(name, "brightness") == 0) cfg.brightness = value.as<int>();
        else if (strcmp(name, "auto_brightness") == 0 && value.is<bool>()) cfg.auto_brightness = value.as<bool>();
        else if (strcmp(name, "show_delta") == 0 && value.is<bool>()) cfg.show_delta = value.as<bool>();
        else if (strcmp(name, "use_mmol") == 0 && value.is<bool>()) cfg.use_mmol = value.as<bool>();
        else if (strcmp(name, "time_display_enabled") == 0 && value.is<bool>()) cfg.time_display_enabled = value.as<bool>();
        else if (strcmp(name, "default_mode") == 0) cfg.default_mode = value.as<int>();
        else if (strcmp(name, "ambient_use_glucose_colors") == 0) cfg.ambient_use_glucose_colors = value.as<bool>();
        else if (strcmp(name, "ambient_style") == 0) cfg.ambient_style = value.as<int>();
        else if (strcmp(name, "ambient_creature") == 0 && !changes.containsKey("ambient_character")) cfg.ambient_character = value.as<int>();
        else if (strcmp(name, "ambient_character") == 0) cfg.ambient_character = value.as<int>();
        else if (strcmp(name, "ambient_enabled") == 0 && value.is<bool>()) cfg.ambient_enabled = value.as<bool>();
        else if (strcmp(name, "ambient_seasonal") == 0 && value.is<bool>()) cfg.ambient_seasonal = value.as<bool>();
        else if (strcmp(name, "notify_enabled") == 0 && value.is<bool>()) cfg.notify_enabled = value.as<bool>();
        else if (strcmp(name, "auto_cycle_enabled") == 0 && value.is<bool>()) cfg.auto_cycle_enabled = value.as<bool>();
        else if (strcmp(name, "auto_cycle_sec") == 0) cfg.auto_cycle_sec = value.as<int>();
    }
    if (!cfg.time_display_enabled && cfg.default_mode == 1) cfg.default_mode = 0;
    if (!cfg.ambient_enabled && cfg.default_mode == 3) cfg.default_mode = 0;
    config_save();
    engine_rebuild_toggle_order();
    return true;
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
        config_get().auto_update_enabled = !payload["paused"].as<bool>();
        config_save();
        return post_result(id, "succeeded");
    }
    if (strcmp(type, "ota_check") == 0) {
        // The check-in itself obtains the current authoritative offer.
        return post_result(id, "succeeded");
    }
    if (strcmp(type, "ota_install") == 0) {
        // Old queued JSON installs cannot bypass current rollout authorization.
        return post_result(id, "failed", "requires_rollout_target");
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

static void handle_update_offer(JsonObjectConst offer, bool manual) {
    if (offer.isNull() || pending_ota_command[0] || ota_is_busy()) return;
    const char* id = offer["target_id"] | "";
    const char* version = offer["version"] | "";
    if (strlen(id) != 36 || !*version || strlen(version) >= sizeof(pending_ota_version)) return;
    const char* local_block = fleet_local_install_block(
        manual, config_get().auto_update_enabled && maintenance_automatic, minute_in_window());
    if (local_block) {
        post_result(id, "deferred", local_block, nullptr, true);
        return;
    }
    if (!save_pending_ota(id, version, offer["channel"] | "")) {
        clear_pending_ota();
        post_result(id, "failed", "persistence_failed", nullptr, true);
        return;
    }
    pending_manual_install = manual;
    OtaRequestResult queued = ota_request_managed_install(
        offer["manifest_url"] | "", version, offer["channel"] | "", offer["sha256"] | "");
    if (queued != OTA_REQUEST_QUEUED && !pending_outcome[0]) {
        // start_worker already persisted task-create failure classification.
        // Never overwrite that deferred outcome with a generic terminal failure.
        fleet_record_update_outcome(queued == OTA_REQUEST_BUSY, "invalid_managed_offer");
    }
}

bool fleet_authorize_update(const char* manifest_url, const char* version,
                            const char* release_channel, const char* sha256) {
    if (!pending_is_target || !pending_ota_command[0] ||
        fleet_local_install_block(pending_manual_install,
            config_get().auto_update_enabled && maintenance_automatic, minute_in_window())) return false;
    JsonDocument doc;
    doc["installation_id"] = installation_id;
    String body, response;
    serializeJson(doc, body);
    String path = String("/device/v1/updates/") + pending_ota_command + "/authorize";
    if (post_json(path.c_str(), body, response) != 200) return false;
    JsonDocument result;
    if (deserializeJson(result, response) || !(result["authorized"] | false) ||
        (result["expires_at"] | 0L) <= time(nullptr)) return false;
    JsonObjectConst offer = result["update_offer"].as<JsonObjectConst>();
    if (strcmp(offer["target_id"] | "", pending_ota_command) ||
        strcmp(offer["manifest_url"] | "", manifest_url) ||
        strcmp(offer["version"] | "", version) ||
        strcmp(offer["channel"] | "", release_channel) ||
        strcmp(offer["sha256"] | "", sha256)) return false;
    Preferences prefs;
    if (!prefs.begin(FLEET_NAMESPACE, false)) return false;
    bool saved = prefs.putBool("ota_started", true);
    prefs.end();
    if (!saved) return false;
    pending_install_authorized = true;
    authorization_expires_at = result["expires_at"] | 0L;
    if (!fleet_update_authorization_current()) return false;
    // An authorization is the server's durable start record. No network call
    // between this fresh authorization and starting the verified image install.
    return true;
}

bool fleet_update_authorization_current() {
    return pending_is_target && pending_ota_command[0] && pending_install_authorized &&
           fleet_authorization_valid(time(nullptr), authorization_expires_at, time_is_available(),
                                     pending_manual_install || (config_get().auto_update_enabled && maintenance_automatic),
                                     pending_manual_install || minute_in_window());
}

static bool check_in(uint32_t& next_seconds, bool manual, bool allow_offer) {
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
    unsigned start_minute = maintenance_custom ? maintenance_start : config_get().auto_update_hour * 60;
    unsigned end_minute = maintenance_custom ? maintenance_end : (start_minute + 60) % 1440;
    snprintf(start, sizeof(start), "%02u:%02u", start_minute / 60, start_minute % 60);
    snprintf(end, sizeof(end), "%02u:%02u", end_minute / 60, end_minute % 60);
    window["start"] = start;
    window["end"] = end;
    window["automatic_install"] = maintenance_automatic && config_get().auto_update_enabled;
    doc["health_codes"].to<JsonArray>();
    doc["capabilities"].to<JsonArray>().add("fleet_rollout_v1");
    const AppConfig& cfg = config_get();
    JsonObject features = doc["features"].to<JsonObject>();
    features["schema_version"] = 1;
    static const char* sources[] = {"custom", "dexcom", "demo", "libre"};
    if (cfg.data_source >= 0 && cfg.data_source < 4) features["data_source"] = sources[cfg.data_source];
    features["companion_enabled"] = cfg.ambient_enabled;
    features["companion_character"] = companion_or_default(cfg.ambient_character);
    features["weather_enabled"] = cfg.weather_enabled;
    features["timer_enabled"] = cfg.timer_enabled;
    features["stopwatch_enabled"] = cfg.stopwatch_enabled;
    features["notifications_enabled"] = cfg.notify_enabled;
    features["sysmon_enabled"] = cfg.sysmon_enabled;
    features["auto_cycle_enabled"] = cfg.auto_cycle_enabled;
    features["countdown_enabled"] = cfg.countdown_enabled;
    features["night_mode_enabled"] = cfg.night_mode_enabled;
    features["auto_brightness"] = cfg.auto_brightness;
    features["time_display_enabled"] = cfg.time_display_enabled;

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
    next_seconds = result["next_checkin_seconds"] | 300;
    // Bound a fleet visit even if a server sends a large command batch. Remaining
    // commands stay retryable on the service, preserving time for core requests.
    for (JsonObjectConst command : result["commands"].as<JsonArrayConst>()) {
        handle_command(command);
        break;
    }
    if (!restart_requested && allow_offer)
        handle_update_offer(result["update_offer"].as<JsonObjectConst>(), manual);
    return true;
}

static void fleet_worker(void*) {
    // Consume even if reporting/network fails: a failed visit does not leave a
    // standing permission to install during an unrelated automatic visit.
    portENTER_CRITICAL(&manual_intent_mux);
    bool manual = manual_intent.consume(millis());
    portEXIT_CRITICAL(&manual_intent_mux);
    uint32_t next_seconds = 300;
    bool had_pending_outcome = pending_ota_command[0] != '\0';
    bool ok = report_pending_ota();
    if (ok && !registered) ok = register_device(next_seconds);
    // Reporting a transient outcome promptly must not immediately launch the
    // same attempt again in a tight loop. Automatic retries wait one heartbeat;
    // only a new explicit manual request may retry in this reporting visit.
    if (ok && registered) ok = check_in(next_seconds, manual, !had_pending_outcome || manual);
    if (ok) {
        failure_count = 0;
        next_seconds = fleet_checkin_seconds(next_seconds);
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
    if (worker_running || ota_is_busy() || !wifi_is_connected() || wifi_is_ap_mode() ||
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

const char* fleet_installation_id() { return installation_id; }

bool fleet_request_check() {
    if (worker_running || ota_is_busy()) return false;
    next_attempt_ms = millis();
    return true;
}

bool fleet_request_manual_install() {
    if (worker_running || ota_is_busy()) return false;
    portENTER_CRITICAL(&manual_intent_mux);
    manual_intent.request(millis());
    portEXIT_CRITICAL(&manual_intent_mux);
    next_attempt_ms = millis();
    return true;
}

bool fleet_record_update_outcome(bool deferred, const char* error) {
    if (!pending_is_target || !pending_ota_command[0]) return deferred;
    bool transient = !deferred && fleet_preflight_error_is_retryable(error, pending_install_authorized);
    deferred = deferred || transient;
    copy_text(pending_outcome_reason, sizeof(pending_outcome_reason),
              transient ? "transient_update_error" : deferred ? "local_safety" : "update_failed");
    copy_text(pending_outcome, sizeof(pending_outcome), deferred ? "deferred" : "failed");
    Preferences prefs;
    if (prefs.begin(FLEET_NAMESPACE, false)) {
        prefs.putString("ota_outcome", pending_outcome);
        prefs.putString("ota_reason", pending_outcome_reason);
        prefs.end();
    }
    next_attempt_ms = millis();
    return deferred;
}
