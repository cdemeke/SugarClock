#include "ota_manager.h"

#include "buzzer.h"
#include "config_manager.h"
#include "display.h"
#include "glucose_engine.h"
#include "http_client.h"
#include "improv_serial.h"
#include "notify_engine.h"
#include "ota_manifest.h"
#include "ota_policy.h"
#include "ota_trusted_roots.h"
#include "semver.h"
#include "sensors.h"
#include "time_engine.h"
#include "timer_engine.h"
#include "weather_client.h"
#include "web_assets.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <time.h>

#ifndef SUGARCLOCK_VERSION
#error "SUGARCLOCK_VERSION must be injected from VERSION"
#endif
#ifndef SUGARCLOCK_OTA_MANIFEST_URL
#define SUGARCLOCK_OTA_MANIFEST_URL "https://github.com/cdemeke/SugarClock/releases/latest/download/ota-manifest.json"
#endif

static const uint32_t OTA_DOWNLOAD_TIMEOUT_MS = 30000;
static const uint32_t OTA_VALIDATION_PERIOD_MS = 15000;
static const uint32_t OTA_VALIDATION_MIN_HEAP = 55000;
static const char* OTA_NVS_NAMESPACE = "sugarota";

static portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;
static OtaStatusSnapshot status_snapshot;
static OtaManifest available_manifest;
static volatile bool manifest_available = false;
static volatile bool worker_running = false;
static bool validation_pending = false;
static uint32_t validation_started_ms = 0;
static uint32_t validation_loop_count = 0;
static int scheduled_jitter_min = 0;
static int last_checked_day = -1;
static unsigned retry_failures = 0;
static uint32_t next_retry_ms = 0;
static uint32_t last_render_ms = 0;

class CappedManifestStream : public Stream {
public:
    CappedManifestStream(char* buffer, size_t capacity)
        : buffer_(buffer), capacity_(capacity), length_(0), overflow_(false) {}

    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t* data, size_t size) override {
        size_t room = length_ < capacity_ ? capacity_ - length_ : 0;
        size_t copied = size < room ? size : room;
        if (copied) memcpy(buffer_ + length_, data, copied);
        length_ += copied;
        if (copied != size) overflow_ = true;
        // Report consumption so HTTPClient can drain and close the response.
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t length() const { return length_; }
    bool overflowed() const { return overflow_; }

private:
    char* buffer_;
    size_t capacity_;
    size_t length_;
    bool overflow_;
};

static void copy_text(char* destination, size_t size, const char* source) {
    if (!destination || size == 0) return;
    snprintf(destination, size, "%s", source ? source : "");
}

static void set_state(OtaState state, const char* error = nullptr,
                      const char* safety = nullptr) {
    portENTER_CRITICAL(&status_mux);
    status_snapshot.state = state;
    if (error) copy_text(status_snapshot.last_error, sizeof(status_snapshot.last_error), error);
    if (safety) copy_text(status_snapshot.safety_reason, sizeof(status_snapshot.safety_reason), safety);
    else if (state != OTA_DEFERRED) status_snapshot.safety_reason[0] = '\0';
    portEXIT_CRITICAL(&status_mux);
}

static void set_progress(int progress) {
    portENTER_CRITICAL(&status_mux);
    status_snapshot.progress = constrain(progress, 0, 100);
    portEXIT_CRITICAL(&status_mux);
}

const char* ota_state_name(OtaState state) {
    switch (state) {
        case OTA_IDLE: return "idle";
        case OTA_CHECKING: return "checking";
        case OTA_UPDATE_AVAILABLE: return "update_available";
        case OTA_DEFERRED: return "deferred";
        case OTA_DOWNLOADING: return "downloading";
        case OTA_VERIFYING: return "verifying";
        case OTA_PENDING_REBOOT: return "pending_reboot";
        case OTA_ERROR: return "error";
        default: return "unknown";
    }
}

static bool state_is_busy(OtaState state) {
    return worker_running || state == OTA_CHECKING || state == OTA_DOWNLOADING ||
           state == OTA_VERIFYING || state == OTA_PENDING_REBOOT;
}

bool ota_is_busy() {
    OtaState state;
    portENTER_CRITICAL(&status_mux);
    state = status_snapshot.state;
    portEXIT_CRITICAL(&status_mux);
    return state_is_busy(state);
}

static OtaSafetyInputs collect_safety_inputs() {
    AppConfig& cfg = config_get();
    const GlucoseReading& reading = http_get_reading();
    bool urgent_glucose = reading.valid &&
        (reading.glucose < cfg.thresh_urgent_low || reading.glucose > cfg.thresh_urgent_high);
    TimerState timer_state = timer_get_state();

    OtaSafetyInputs inputs = {};
    inputs.wifi_connected = wifi_is_connected();
    inputs.time_available = time_is_available();
    inputs.setup_or_ap_active = wifi_is_ap_mode() || improv_is_active();
    inputs.buzzer_active = buzzer_is_active();
    inputs.urgent_notification = notify_is_urgent();
    inputs.urgent_glucose = urgent_glucose;
    inputs.timer_running = timer_state == TIMER_RUNNING || timer_state == TIMER_BREAK ||
                           timer_state == TIMER_LONG_BREAK;
    inputs.stopwatch_running = stopwatch_get_state() == SW_RUNNING;
    inputs.battery_percent = sensors_get_battery_percent();
    inputs.free_heap = ESP.getFreeHeap();
    return inputs;
}

bool ota_automatic_install_is_safe() {
    return ota_safety_failure(collect_safety_inputs()) == nullptr;
}

static String resolve_redirect(const String& current, const String& location) {
    if (location.startsWith("https://")) return location;
    if (!location.startsWith("/")) return String();
    int scheme_end = current.indexOf("//");
    if (scheme_end < 0) return String();
    int path_start = current.indexOf('/', scheme_end + 2);
    String origin = path_start < 0 ? current : current.substring(0, path_start);
    return origin + location;
}

static int open_https_with_redirects(HTTPClient& http, WiFiClientSecure& client,
                                     String& url, char* error, size_t error_size) {
    for (int redirect = 0; redirect <= 5; ++redirect) {
        if (!url.startsWith("https://")) {
            copy_text(error, error_size, "redirect_not_https");
            return -1;
        }
        if (!http.begin(client, url)) {
            copy_text(error, error_size, "https_begin_failed");
            return -1;
        }
        http.setConnectTimeout(10000);
        http.setTimeout(15000);
        http.setUserAgent(String("SugarClock/") + SUGARCLOCK_VERSION);
        int code = http.GET();
        if (code != HTTP_CODE_MOVED_PERMANENTLY && code != HTTP_CODE_FOUND &&
            code != HTTP_CODE_TEMPORARY_REDIRECT && code != HTTP_CODE_PERMANENT_REDIRECT) {
            return code;
        }
        String next = resolve_redirect(url, http.getLocation());
        http.end();
        if (next.isEmpty()) {
            copy_text(error, error_size, "invalid_https_redirect");
            return -1;
        }
        url = next;
    }
    copy_text(error, error_size, "too_many_redirects");
    return -1;
}

static bool download_manifest(OtaManifest& manifest, char* error, size_t error_size) {
    WiFiClientSecure client;
    client.setCACert(OTA_TRUSTED_ROOTS_PEM);
    client.setHandshakeTimeout(15);
    HTTPClient http;
    String url = SUGARCLOCK_OTA_MANIFEST_URL;
    int code = open_https_with_redirects(http, client, url, error, error_size);
    if (code != HTTP_CODE_OK) {
        if (code >= 0) snprintf(error, error_size, "manifest_http_%d", code);
        http.end();
        return false;
    }

    int content_length = http.getSize();
    if (content_length > OTA_MANIFEST_MAX_BYTES) {
        copy_text(error, error_size, "manifest_too_large");
        http.end();
        return false;
    }

    char* body = static_cast<char*>(malloc(OTA_MANIFEST_MAX_BYTES + 1));
    if (!body) {
        copy_text(error, error_size, "heap_low");
        http.end();
        return false;
    }
    CappedManifestStream sink(body, OTA_MANIFEST_MAX_BYTES);
    int written = http.writeToStream(&sink);
    http.end();
    if (written < 0 || sink.overflowed() || sink.length() == 0) {
        copy_text(error, error_size, sink.overflowed() ? "manifest_too_large" : "manifest_read_failed");
        free(body);
        return false;
    }
    body[sink.length()] = '\0';
    bool parsed = ota_manifest_parse(body, sink.length(), manifest, error, error_size);
    free(body);
    return parsed;
}

static bool constant_time_equal(const uint8_t* a, const uint8_t* b, size_t length) {
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= a[i] ^ b[i];
    return difference == 0;
}

static bool hex_to_bytes(const char* hex, uint8_t* output, size_t output_size) {
    if (!hex || strlen(hex) != output_size * 2) return false;
    for (size_t i = 0; i < output_size; ++i) {
        char pair[3] = {hex[i * 2], hex[i * 2 + 1], 0};
        char* end = nullptr;
        unsigned long value = strtoul(pair, &end, 16);
        if (!end || *end != '\0') return false;
        output[i] = static_cast<uint8_t>(value);
    }
    return true;
}

static bool save_pending_metadata(const OtaManifest& manifest,
                                  const esp_partition_t* target) {
    Preferences prefs;
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) return false;
    bool ok = prefs.putBool("pending", true) &&
              prefs.putString("previous", SUGARCLOCK_VERSION) > 0 &&
              prefs.putString("new", manifest.version) > 0 &&
              prefs.putString("partition", target ? target->label : "") > 0;
    prefs.end();
    return ok;
}

static void clear_pending_metadata() {
    Preferences prefs;
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) return;
    prefs.remove("pending");
    prefs.remove("previous");
    prefs.remove("new");
    prefs.remove("partition");
    prefs.end();
}

static bool install_firmware(const OtaManifest& manifest, char* error, size_t error_size) {
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) return (copy_text(error, error_size, "no_inactive_partition"), false);
    if (manifest.size > target->size) return (copy_text(error, error_size, "firmware_too_large"), false);

    WiFiClientSecure client;
    client.setCACert(OTA_TRUSTED_ROOTS_PEM);
    client.setHandshakeTimeout(15);
    HTTPClient http;
    String url = manifest.firmware_url;
    int code = open_https_with_redirects(http, client, url, error, error_size);
    if (code != HTTP_CODE_OK) {
        if (code >= 0) snprintf(error, error_size, "firmware_http_%d", code);
        http.end();
        return false;
    }
    if (http.getSize() != static_cast<int>(manifest.size)) {
        copy_text(error, error_size, "firmware_size_mismatch");
        http.end();
        return false;
    }

    esp_ota_handle_t handle = 0;
    bool ota_active = false;
    if (esp_ota_begin(target, manifest.size, &handle) != ESP_OK) {
        copy_text(error, error_size, "ota_begin_failed");
        http.end();
        return false;
    }
    ota_active = true;

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[4096];
    uint32_t received = 0;
    uint32_t last_data_ms = millis();
    bool ok = true;

    while (received < manifest.size) {
        int available = stream->available();
        if (available <= 0) {
            if (!http.connected() || millis() - last_data_ms > OTA_DOWNLOAD_TIMEOUT_MS) {
                copy_text(error, error_size, !http.connected() ? "firmware_short_read" : "firmware_timeout");
                ok = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        size_t remaining = manifest.size - received;
        size_t chunk = min(static_cast<size_t>(available), min(sizeof(buffer), remaining));
        int count = stream->read(buffer, chunk);
        if (count <= 0) continue;
        last_data_ms = millis();
        mbedtls_sha256_update_ret(&sha, buffer, count);
        if (esp_ota_write(handle, buffer, count) != ESP_OK) {
            copy_text(error, error_size, "ota_write_failed");
            ok = false;
            break;
        }
        received += count;
        set_progress(static_cast<int>((static_cast<uint64_t>(received) * 100ULL) / manifest.size));
    }
    http.end();

    uint8_t actual_sha[32];
    mbedtls_sha256_finish_ret(&sha, actual_sha);
    mbedtls_sha256_free(&sha);
    uint8_t expected_sha[32];
    if (ok && (!hex_to_bytes(manifest.sha256, expected_sha, sizeof(expected_sha)) ||
               !constant_time_equal(actual_sha, expected_sha, sizeof(actual_sha)))) {
        copy_text(error, error_size, "sha256_mismatch");
        ok = false;
    }

    if (ok) {
        set_state(OTA_VERIFYING);
        esp_err_t end_result = esp_ota_end(handle);
        ota_active = false;  // esp_ota_end consumes the handle, success or failure.
        if (end_result != ESP_OK) {
            copy_text(error, error_size, "invalid_esp_image");
            ok = false;
        }
    }
    if (!ok && ota_active) esp_ota_abort(handle);
    if (!ok) return false;

    esp_app_desc_t description = {};
    if (esp_ota_get_partition_description(target, &description) != ESP_OK) {
        copy_text(error, error_size, "image_description_failed");
        return false;
    }
    SemVer image_version = SemVer::parse(description.version);
    if (image_version.valid && image_version != SemVer::parse(manifest.version)) {
        copy_text(error, error_size, "image_version_mismatch");
        return false;
    }
    if (!image_version.valid) {
        Serial.printf("[OTA] Framework image metadata version '%s' is non-semantic; signed manifest version used\n",
                      description.version);
    }

    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        copy_text(error, error_size, "set_boot_partition_failed");
        return false;
    }
    if (!save_pending_metadata(manifest, target)) {
        const esp_partition_t* running = esp_ota_get_running_partition();
        if (running) esp_ota_set_boot_partition(running);
        clear_pending_metadata();
        copy_text(error, error_size, "pending_metadata_failed");
        return false;
    }
    Serial.printf("[OTA] Verified %lu bytes; boot partition set to %s for v%s\n",
                  static_cast<unsigned long>(received), target->label, manifest.version);
    return true;
}

static void record_check_success() {
    time_t now = time(nullptr);
    struct tm local = {};
    localtime_r(&now, &local);
    last_checked_day = local.tm_year * 366 + local.tm_yday;
    retry_failures = 0;
    next_retry_ms = 0;
    Preferences prefs;
    if (prefs.begin(OTA_NVS_NAMESPACE, false)) {
        prefs.putInt("last_day", last_checked_day);
        prefs.putULong("last_check", static_cast<uint32_t>(now));
        prefs.putUInt("failures", 0);
        prefs.end();
    }
    portENTER_CRITICAL(&status_mux);
    status_snapshot.last_check = static_cast<uint32_t>(now);
    status_snapshot.last_error[0] = '\0';
    portEXIT_CRITICAL(&status_mux);
}

static void record_failure(const char* error) {
    ++retry_failures;
    next_retry_ms = millis() + ota_retry_delay_ms(retry_failures);
    Preferences prefs;
    if (prefs.begin(OTA_NVS_NAMESPACE, false)) {
        prefs.putUInt("failures", retry_failures);
        prefs.putString("last_error", error ? error : "unknown");
        prefs.end();
    }
    set_state(OTA_ERROR, error ? error : "unknown");
    Serial.printf("[OTA] Failed: %s\n", error ? error : "unknown");
}

static void ota_worker(void* parameter) {
    bool install = reinterpret_cast<uintptr_t>(parameter) != 0;
    char error[64] = "";

    if (!wifi_is_connected()) {
        record_failure("wifi_unavailable");
        worker_running = false;
        vTaskDelete(nullptr);
        return;
    }
    if (!time_is_available()) {
        record_failure("time_unavailable");
        worker_running = false;
        vTaskDelete(nullptr);
        return;
    }

    if (!install) {
        OtaManifest candidate = {};
        const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
        if (!target) copy_text(error, sizeof(error), "no_inactive_partition");
        else if (!download_manifest(candidate, error, sizeof(error))) {}
        else if (!ota_manifest_validate_identity_and_formats(candidate, error, sizeof(error))) {}
        else if (!ota_manifest_verify_signature(candidate, error, sizeof(error))) {}
        else if (!ota_manifest_validate_offer(candidate, SUGARCLOCK_VERSION, target->size,
                                               error, sizeof(error))) {
            if (strcmp(error, "not_newer") == 0) {
                manifest_available = false;
                portENTER_CRITICAL(&status_mux);
                status_snapshot.available_version[0] = '\0';
                portEXIT_CRITICAL(&status_mux);
                record_check_success();
                set_state(OTA_IDLE);
                Serial.println("[OTA] Firmware is current");
                worker_running = false;
                vTaskDelete(nullptr);
                return;
            }
        } else {
            available_manifest = candidate;
            manifest_available = true;
            portENTER_CRITICAL(&status_mux);
            copy_text(status_snapshot.available_version, sizeof(status_snapshot.available_version), candidate.version);
            portEXIT_CRITICAL(&status_mux);
            record_check_success();
            set_state(OTA_UPDATE_AVAILABLE);
            Serial.printf("[OTA] Update v%s is available\n", candidate.version);
            worker_running = false;
            vTaskDelete(nullptr);
            return;
        }
        record_failure(error[0] ? error : "manifest_check_failed");
    } else {
        const char* safety = ota_safety_failure(collect_safety_inputs());
        if (safety) {
            set_state(OTA_DEFERRED, nullptr, safety);
            worker_running = false;
            vTaskDelete(nullptr);
            return;
        }
        http_set_paused(true);
        weather_set_paused(true);
        set_state(OTA_DOWNLOADING);
        set_progress(0);
        if (install_firmware(available_manifest, error, sizeof(error))) {
            set_progress(100);
            set_state(OTA_PENDING_REBOOT);
            Serial.println("[OTA] Update complete; rebooting");
            delay(1000);
            ESP.restart();
        } else {
            http_set_paused(false);
            weather_set_paused(false);
            record_failure(error[0] ? error : "install_failed");
        }
    }

    worker_running = false;
    vTaskDelete(nullptr);
}

static OtaRequestResult start_worker(bool install) {
    if (worker_running) return OTA_REQUEST_BUSY;
    worker_running = true;
    BaseType_t result = xTaskCreate(ota_worker, install ? "ota_install" : "ota_check",
                                   12288, reinterpret_cast<void*>(install ? 1U : 0U),
                                   1, nullptr);
    if (result != pdPASS) {
        worker_running = false;
        set_state(OTA_ERROR, "task_create_failed");
        return OTA_REQUEST_INTERNAL_ERROR;
    }
    return OTA_REQUEST_QUEUED;
}

OtaRequestResult ota_request_check() {
    if (ota_is_busy()) return OTA_REQUEST_BUSY;
    set_state(OTA_CHECKING);
    set_progress(0);
    return start_worker(false);
}

OtaRequestResult ota_request_install(bool manual) {
    if (ota_is_busy()) return OTA_REQUEST_BUSY;
    if (!manifest_available) return OTA_REQUEST_NO_UPDATE;
    AppConfig& cfg = config_get();
    const char* safety = ota_safety_failure(collect_safety_inputs());
    if (!safety && !manual && !ota_in_install_window(time_get_hour(), cfg.auto_update_hour)) {
        safety = "outside_install_window";
    }
    if (safety) {
        set_state(OTA_DEFERRED, nullptr, safety);
        return OTA_REQUEST_UNSAFE;
    }
    return start_worker(true);
}

static void inspect_boot_state() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    copy_text(status_snapshot.running_partition, sizeof(status_snapshot.running_partition), running ? running->label : "unknown");
    copy_text(status_snapshot.boot_partition, sizeof(status_snapshot.boot_partition), boot ? boot->label : "unknown");
    Serial.printf("[OTA] Running partition: %s; boot partition: %s\n",
                  status_snapshot.running_partition, status_snapshot.boot_partition);

    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &image_state) == ESP_OK &&
        image_state == ESP_OTA_IMG_PENDING_VERIFY) {
        validation_pending = true;
        validation_started_ms = millis();
        status_snapshot.pending_verification = true;
        Serial.printf("[OTA] v%s pending local first-boot validation\n", SUGARCLOCK_VERSION);
    }

    Preferences prefs;
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) return;
    bool pending_record = prefs.getBool("pending", false);
    String previous = prefs.getString("previous", "");
    String next = prefs.getString("new", "");
    last_checked_day = prefs.getInt("last_day", -1);
    retry_failures = prefs.getUInt("failures", 0);
    status_snapshot.last_check = prefs.getULong("last_check", 0);
    String saved_error = prefs.getString("last_error", "");
    copy_text(status_snapshot.last_error, sizeof(status_snapshot.last_error), saved_error.c_str());
    prefs.end();

    if (pending_record) {
        Serial.printf("[OTA] Pending metadata: previous=%s new=%s\n", previous.c_str(), next.c_str());
        if (!validation_pending && next != SUGARCLOCK_VERSION) {
            Serial.printf("[OTA] Rollback detected: v%s did not validate; restored v%s\n",
                          next.c_str(), SUGARCLOCK_VERSION);
            set_state(OTA_ERROR, "rollback_detected");
            clear_pending_metadata();
        } else if (!validation_pending && next == SUGARCLOCK_VERSION) {
            clear_pending_metadata();
        }
    }
}

void ota_init() {
    memset(&status_snapshot, 0, sizeof(status_snapshot));
    status_snapshot.state = OTA_IDLE;
    copy_text(status_snapshot.current_version, sizeof(status_snapshot.current_version), SUGARCLOCK_VERSION);
    scheduled_jitter_min = static_cast<int>(esp_random() % 46U);
    inspect_boot_state();
    Serial.printf("[OTA] Automatic checks enabled with %d minute daily jitter\n", scheduled_jitter_min);
}

static void validate_pending_image() {
    if (!validation_pending) return;
    ++validation_loop_count;
    if (millis() - validation_started_ms < OTA_VALIDATION_PERIOD_MS) return;

    bool local_health_ok = config_is_loaded() && get_web_assets_count() > 0 &&
                           validation_loop_count > 100 && ESP.getFreeHeap() >= OTA_VALIDATION_MIN_HEAP;
    if (local_health_ok) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            validation_pending = false;
            status_snapshot.pending_verification = false;
            clear_pending_metadata();
            Serial.printf("[OTA] Local validation passed; v%s marked valid\n", SUGARCLOCK_VERSION);
        }
    } else {
        Serial.printf("[OTA] Local validation failed (config=%d assets=%u loops=%lu heap=%u); rolling back\n",
                      config_is_loaded(), static_cast<unsigned>(get_web_assets_count()),
                      static_cast<unsigned long>(validation_loop_count), ESP.getFreeHeap());
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

static void render_update_status() {
    OtaStatusSnapshot snapshot;
    ota_get_status(snapshot);
    if (snapshot.state != OTA_DOWNLOADING && snapshot.state != OTA_VERIFYING &&
        snapshot.state != OTA_PENDING_REBOOT) return;
    if (millis() - last_render_ms < 200) return;
    last_render_ms = millis();
    char text[8];
    if (snapshot.state == OTA_DOWNLOADING) snprintf(text, sizeof(text), "%d%%", snapshot.progress);
    else if (snapshot.state == OTA_VERIFYING) snprintf(text, sizeof(text), "VERIFY");
    else snprintf(text, sizeof(text), "REBOOT");
    display_clear();
    display_draw_text(text, 1, 0, display_color(0, 200, 200));
    display_show();
}

void ota_loop() {
    validate_pending_image();
    render_update_status();

    AppConfig& cfg = config_get();
    portENTER_CRITICAL(&status_mux);
    status_snapshot.auto_update_enabled = cfg.auto_update_enabled;
    status_snapshot.auto_update_hour = cfg.auto_update_hour;
    OtaState state = status_snapshot.state;
    portEXIT_CRITICAL(&status_mux);

    if (!cfg.auto_update_enabled || !wifi_is_connected() || !time_is_available() || ota_is_busy()) return;

    if ((state == OTA_UPDATE_AVAILABLE || state == OTA_DEFERRED) &&
        ota_in_install_window(time_get_hour(), cfg.auto_update_hour)) {
        ota_request_install(false);
        return;
    }

    time_t now = time(nullptr);
    struct tm local = {};
    localtime_r(&now, &local);
    int today = local.tm_year * 366 + local.tm_yday;
    int minute_of_day = local.tm_hour * 60 + local.tm_min;
    int scheduled_minute = cfg.auto_update_hour * 60 + scheduled_jitter_min;
    bool retry_ready = next_retry_ms == 0 || static_cast<int32_t>(millis() - next_retry_ms) >= 0;
    if (today != last_checked_day && minute_of_day >= scheduled_minute && retry_ready) {
        ota_request_check();
    }
}

void ota_get_status(OtaStatusSnapshot& output) {
    portENTER_CRITICAL(&status_mux);
    output = status_snapshot;
    portEXIT_CRITICAL(&status_mux);
}
