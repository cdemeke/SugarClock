#include "http_client.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "nightscout_client.h"
#include <limits.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Dexcom Share constants
#define DEXCOM_APP_ID "d89443d2-327c-4a6f-89e5-496bbb0317db"
#define DEXCOM_US_BASE "https://share2.dexcom.com/ShareWebServices/Services"
#define DEXCOM_OUS_BASE "https://shareous1.dexcom.com/ShareWebServices/Services"
#define DEXCOM_AUTH_PATH "/General/AuthenticatePublisherAccount"
#define DEXCOM_LOGIN_PATH "/General/LoginPublisherAccountById"
#define DEXCOM_GLUCOSE_PATH "/Publisher/ReadPublisherLatestGlucoseValues"
#define DEXCOM_NULL_SESSION "00000000-0000-0000-0000-000000000000"
#define DEXCOM_SESSION_LIFETIME_MS (3600000UL) // re-auth every hour

// Per-call timeouts. Kept below the 30s task watchdog so a single blocking
// connect + read can never starve the WDT between feeds.
#define HTTP_TIMEOUT_SEC  10
#define HTTP_TIMEOUT_MS   10000

// Fetches run only on the background network task (net_task.cpp). The data
// below is also read from the UI loop (core 1) and the async web server
// task, so struct/string state is guarded by data_mutex and word-sized
// counters are volatile (32-bit loads/stores are atomic on ESP32).
static SemaphoreHandle_t data_mutex = NULL;
static SemaphoreHandle_t force_done_sem = NULL;
static SemaphoreHandle_t force_caller_mutex = NULL;
// Request identity prevents a timed-out request completing a later request.
static uint32_t force_next_id = 0;
static uint32_t force_pending_id = 0;
static uint32_t force_done_id = 0;
static bool force_result = false;
static char force_error[128] = "";

static GlucoseReading current_reading;            // guarded by data_mutex
static volatile int failure_count = 0;
static volatile int last_response_code = 0;
static char last_response_body[512] = "";         // guarded by data_mutex
static volatile bool ever_received = false;
static unsigned long last_poll_ms = 0;            // network task only
static volatile unsigned long last_success_ms = 0;
static volatile bool http_paused = false;
static bool sensor_age_active = false;  // guarded by data_mutex
static uint32_t sensor_age_ms = 0;
static unsigned long sensor_received_ms = 0;
static bool delta_available = true;

// Delta tracking (guarded by data_mutex)
static int prev_glucose = 0;
static volatile int current_delta = 0;
static bool has_prev_reading = false;
static unsigned long last_recorded_timestamp = 0;

// History circular buffer (guarded by data_mutex)
static GlucoseHistoryEntry history_buf[GLUCOSE_HISTORY_SIZE];
static int history_write_idx = 0;
static int history_count = 0;

// Dexcom session state (network task only)
static char dexcom_session_id[64] = "";
static unsigned long dexcom_session_time_ms = 0;

// Demo mode: synthetic glucose that gently wanders in a normal 80-100 range.
// Used for recording videos / demos without a real CGM connection.
#define DEMO_UPDATE_MS 5000
static unsigned long demo_last_update_ms = 0;
static int demo_value = 90;

static void set_last_response(const char* s) {
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    strncpy(last_response_body, s, sizeof(last_response_body) - 1);
    last_response_body[sizeof(last_response_body) - 1] = '\0';
    xSemaphoreGive(data_mutex);
}

// Record a glucose value to history and update delta. Caller must hold
// data_mutex. reading_timestamp is the CGM timestamp (epoch seconds) so we
// can skip duplicate readings that arrive when we poll faster than the CGM
// updates.
static void record_reading(int glucose, unsigned long reading_timestamp) {
    // Skip duplicate readings — same CGM timestamp means same reading
    if (reading_timestamp > 0 && reading_timestamp == last_recorded_timestamp) {
        return;
    }
    last_recorded_timestamp = reading_timestamp;

    if (has_prev_reading) {
        current_delta = glucose - prev_glucose;
    } else {
        current_delta = 0;
    }
    prev_glucose = glucose;
    has_prev_reading = true;

    // Add to history buffer
    history_buf[history_write_idx].glucose = glucose;
    history_buf[history_write_idx].delta = current_delta;
    history_buf[history_write_idx].timestamp = millis();
    history_write_idx = (history_write_idx + 1) % GLUCOSE_HISTORY_SIZE;
    if (history_count < GLUCOSE_HISTORY_SIZE) {
        history_count++;
    }

    Serial.printf("[HTTP] Delta: %+d (prev: %d, now: %d)\n", current_delta, prev_glucose - current_delta, glucose);
}

// Publish a parsed reading for the UI loop and web server to consume.
static void commit_reading(const GlucoseReading& r) {
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    current_reading = r;
    if (r.valid) {
        record_reading(r.glucose, r.timestamp);
    }
    xSemaphoreGive(data_mutex);
}

// Parse trend string to enum
static TrendType parse_trend(const char* trend_str) {
    if (!trend_str) return TREND_UNKNOWN;
    if (strcasecmp(trend_str, "RisingFast") == 0 || strcasecmp(trend_str, "DoubleUp") == 0) return TREND_RISING_FAST;
    if (strcasecmp(trend_str, "Rising") == 0 || strcasecmp(trend_str, "SingleUp") == 0) return TREND_RISING;
    if (strcasecmp(trend_str, "Flat") == 0) return TREND_FLAT;
    if (strcasecmp(trend_str, "FortyFiveUp") == 0) return TREND_RISING;
    if (strcasecmp(trend_str, "FortyFiveDown") == 0) return TREND_FALLING;
    if (strcasecmp(trend_str, "Falling") == 0 || strcasecmp(trend_str, "SingleDown") == 0) return TREND_FALLING;
    if (strcasecmp(trend_str, "FallingFast") == 0 || strcasecmp(trend_str, "DoubleDown") == 0) return TREND_FALLING_FAST;
    return TREND_UNKNOWN;
}

// Parse Dexcom trend number to enum
static TrendType parse_trend_number(int trend) {
    switch (trend) {
        case 1: return TREND_RISING_FAST;   // DoubleUp
        case 2: return TREND_RISING;         // SingleUp
        case 3: return TREND_RISING;         // FortyFiveUp
        case 4: return TREND_FLAT;           // Flat
        case 5: return TREND_FALLING;        // FortyFiveDown
        case 6: return TREND_FALLING;        // SingleDown
        case 7: return TREND_FALLING_FAST;   // DoubleDown
        default: return TREND_UNKNOWN;
    }
}

// Helper: POST JSON to Dexcom endpoint, return response string
static String dexcom_post(const char* url, const String& body, int& httpCode) {
    // Feed the watchdog before each blocking call so multi-step flows
    // (auth -> login -> fetch) never exceed the WDT window in aggregate.
    esp_task_wdt_reset();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_SEC);
    client.setHandshakeTimeout(HTTP_TIMEOUT_SEC);

    HTTPClient http;
    if (!http.begin(client, url)) {
        httpCode = -1;
        return "";
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    httpCode = http.POST(body);
    String response = http.getString();
    http.end();
    return response;
}

// Dexcom Share: two-step authenticate and get session ID
static bool dexcom_login() {
    AppConfig& cfg = config_get();
    const char* base = cfg.dexcom_us ? DEXCOM_US_BASE : DEXCOM_OUS_BASE;

    // Step 1: AuthenticatePublisherAccount (get account ID)
    JsonDocument authDoc;
    authDoc["accountName"] = cfg.dexcom_username;
    authDoc["password"] = cfg.dexcom_password;
    authDoc["applicationId"] = DEXCOM_APP_ID;
    String authBody;
    serializeJson(authDoc, authBody);

    Serial.printf("[DEXCOM] Authenticating (%s)...\n", cfg.dexcom_us ? "US" : "OUS");

    char auth_url[256];
    snprintf(auth_url, sizeof(auth_url), "%s%s", base, DEXCOM_AUTH_PATH);

    int authCode;
    String authResp = dexcom_post(auth_url, authBody, authCode);
    set_last_response(authResp.c_str());
    last_response_code = authCode;

    Serial.printf("[DEXCOM] Auth step 1: HTTP %d\n", authCode);

    if (authCode != HTTP_CODE_OK) {
        Serial.printf("[DEXCOM] Auth failed: HTTP %d\n", authCode);
        return false;
    }

    // Extract account ID from step 1 response
    String accountId = authResp;
    accountId.trim();
    accountId.replace("\"", "");

    // Step 2: LoginPublisherAccountById (get session ID using account ID)
    JsonDocument loginDoc;
    loginDoc["accountId"] = accountId;
    loginDoc["password"] = cfg.dexcom_password;
    loginDoc["applicationId"] = DEXCOM_APP_ID;
    String loginBody;
    serializeJson(loginDoc, loginBody);

    char login_url[256];
    snprintf(login_url, sizeof(login_url), "%s%s", base, DEXCOM_LOGIN_PATH);

    int loginCode;
    String loginResp = dexcom_post(login_url, loginBody, loginCode);
    set_last_response(loginResp.c_str());
    last_response_code = loginCode;

    Serial.printf("[DEXCOM] Auth step 2: HTTP %d\n", loginCode);

    if (loginCode == HTTP_CODE_OK) {
        loginResp.trim();
        loginResp.replace("\"", "");

        // Check for null session (means Share not enabled or no followers)
        if (loginResp == DEXCOM_NULL_SESSION || loginResp.length() < 10) {
            Serial.println("[DEXCOM] Got null session! Dexcom Share may not be enabled.");
            Serial.println("[DEXCOM] Enable Share in Dexcom app: Settings > Share > enable sharing");
            set_last_response("Null session - enable Dexcom Share in app");
            return false;
        }

        strncpy(dexcom_session_id, loginResp.c_str(), sizeof(dexcom_session_id) - 1);
        dexcom_session_time_ms = millis();
        Serial.println("[DEXCOM] Login OK");
        return true;
    }

    Serial.printf("[DEXCOM] Login failed: HTTP %d\n", loginCode);
    return false;
}

// Dexcom Share: fetch latest glucose reading
static bool dexcom_fetch_glucose() {
    AppConfig& cfg = config_get();

    // Check if session needs refresh
    if (strlen(dexcom_session_id) == 0 ||
        (millis() - dexcom_session_time_ms > DEXCOM_SESSION_LIFETIME_MS)) {
        if (!dexcom_login()) {
            failure_count++;
            return false;
        }
    }

    const char* base = cfg.dexcom_us ? DEXCOM_US_BASE : DEXCOM_OUS_BASE;
    char url[384];
    snprintf(url, sizeof(url), "%s%s?sessionId=%s&minutes=10&maxCount=1",
             base, DEXCOM_GLUCOSE_PATH, dexcom_session_id);

    esp_task_wdt_reset();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_SEC);
    client.setHandshakeTimeout(HTTP_TIMEOUT_SEC);

    HTTPClient http;
    if (!http.begin(client, url)) {
        Serial.println("[DEXCOM] Fetch: failed to begin");
        failure_count++;
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Accept", "application/json");

    int httpCode = http.POST(""); // Dexcom requires POST even for reads
    last_response_code = httpCode;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();
        set_last_response(payload.c_str());

        // Parse JSON array response
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[DEXCOM] JSON parse error: %s\n", err.c_str());
            failure_count++;
            return false;
        }

        // Response is an array, get first element
        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() == 0) {
            Serial.println("[DEXCOM] Empty glucose array");
            failure_count++;
            return false;
        }

        JsonObject reading = arr[0];

        GlucoseReading r = {};
        r.glucose = reading["Value"] | 0;
        r.received_at_ms = millis();
        r.force_mode = -1;
        r.message[0] = '\0';

        // Parse trend - can be string or number
        if (reading["Trend"].is<int>()) {
            r.trend = parse_trend_number(reading["Trend"].as<int>());
        } else if (reading["Trend"].is<const char*>()) {
            r.trend = parse_trend(reading["Trend"] | "Unknown");
        } else {
            r.trend = TREND_UNKNOWN;
        }

        // Parse timestamp from "Date(1234567890000)" or "WT" field
        const char* wt = reading["WT"] | reading["ST"] | "";
        if (strlen(wt) > 0) {
            // Extract epoch ms from "Date(1234567890000)" or "/Date(1234567890000)/"
            const char* start = strchr(wt, '(');
            if (start) {
                r.timestamp = (unsigned long)(strtoull(start + 1, NULL, 10) / 1000ULL);
            }
        }

        r.valid = (r.glucose > 0);
        commit_reading(r);

        if (r.valid) {
            failure_count = 0;
            ever_received = true;
            last_success_ms = millis();
            Serial.printf("[DEXCOM] Glucose: %d, Trend: %s\n",
                          r.glucose, TREND_NAMES[r.trend]);
        } else {
            failure_count++;
        }

        return r.valid;
    }

    // Session expired? Try re-login
    if (httpCode == 500) {
        Serial.println("[DEXCOM] Session expired, re-authenticating");
        dexcom_session_id[0] = '\0';
    }

    String resp = http.getString();
    set_last_response(resp.c_str());
    Serial.printf("[DEXCOM] Fetch failed: HTTP %d\n", httpCode);
    failure_count++;
    http.end();
    return false;
}

// Generic URL fetch (original behavior)
static bool generic_fetch() {
    AppConfig& cfg = config_get();

    esp_task_wdt_reset();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_SEC);
    client.setHandshakeTimeout(HTTP_TIMEOUT_SEC);

    HTTPClient http;
    Serial.printf("[HTTP] Polling: %s\n", cfg.server_url);

    if (!http.begin(client, cfg.server_url)) {
        Serial.println("[HTTP] Failed to begin connection");
        failure_count++;
        last_response_code = -1;
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Accept", "application/json");

    if (strlen(cfg.auth_token) > 0) {
        char auth_header[280];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", cfg.auth_token);
        http.addHeader("Authorization", auth_header);
    }

    int httpCode = http.GET();
    last_response_code = httpCode;

    bool success = false;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        set_last_response(payload.c_str());

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[HTTP] JSON parse error: %s\n", err.c_str());
            failure_count++;
        } else {
            GlucoseReading r = {};
            r.glucose = doc["glucose"] | 0;
            r.timestamp = doc["timestamp"] | 0UL;
            r.received_at_ms = millis();
            r.force_mode = doc["force_mode"] | -1;
            r.valid = (r.glucose > 0);

            const char* trend_str = doc["trend"] | "Unknown";
            r.trend = parse_trend(trend_str);

            const char* msg = doc["message"] | "";
            strncpy(r.message, msg, sizeof(r.message) - 1);
            r.message[sizeof(r.message) - 1] = '\0';

            commit_reading(r);

            if (r.valid) {
                failure_count = 0;
                ever_received = true;
                last_success_ms = millis();
                success = true;
                Serial.printf("[HTTP] Glucose: %d, Trend: %s\n",
                              r.glucose, TREND_NAMES[r.trend]);
            } else {
                failure_count++;
                Serial.println("[HTTP] Invalid glucose value");
            }
        }
    } else {
        char errbuf[32];
        snprintf(errbuf, sizeof(errbuf), "HTTP %d", httpCode);
        set_last_response(errbuf);
        Serial.printf("[HTTP] Error: %d\n", httpCode);
        failure_count++;
    }

    http.end();
    return success;
}

// Nightscout publishes sensor age, not the age of a successful HTTP request.
static bool fetch_nightscout_reading() {
    const AppConfig& cfg = config_get(); // protected by the network gate
    NightscoutConfig ns = {};
    memcpy(ns.url, cfg.ns_url, sizeof(ns.url));
    ns.auth_mode = cfg.ns_auth_mode;
    memcpy(ns.credential, cfg.ns_credential, sizeof(ns.credential));
    NightscoutResult result = {};
    bool ok = nightscout_fetch(ns, result);
    last_response_code = result.http_code;
    set_last_response(result.error);
    if (!ok) {
        ++failure_count;
        return false;
    }

    const unsigned long now = millis();
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    if (current_reading.valid && result.timestamp < current_reading.timestamp) {
        xSemaphoreGive(data_mutex);
        set_last_response("Server returned an older reading");
        ++failure_count;
        return false;
    }
    uint64_t incoming_age = static_cast<uint64_t>(result.age_sec) * 1000ULL;
    const bool duplicate = current_reading.valid && result.timestamp == current_reading.timestamp;
    if (duplicate && sensor_age_active) {
        const uint64_t existing_age = static_cast<uint64_t>(sensor_age_ms) +
            static_cast<uint32_t>(now - sensor_received_ms);
        if (incoming_age < existing_age) incoming_age = existing_age;
    }
    sensor_age_ms = incoming_age >= UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(incoming_age);
    sensor_received_ms = now;
    sensor_age_active = true;

    if (!duplicate) {
        const bool local_previous = current_reading.valid &&
            result.timestamp > current_reading.timestamp &&
            result.timestamp - current_reading.timestamp <= 600;
        const int previous = result.has_previous ? result.previous_glucose : current_reading.glucose;
        delta_available = result.has_previous || local_previous;
        if (!has_prev_reading && result.has_previous) {
            record_reading(result.previous_glucose, result.previous_timestamp);
        }
        GlucoseReading r = {};
        r.glucose = result.glucose;
        r.trend = result.trend;
        r.timestamp = result.timestamp;
        r.received_at_ms = now;
        r.force_mode = -1;
        r.valid = true;
        current_reading = r;
        record_reading(r.glucose, r.timestamp);
        current_delta = delta_available ? r.glucose - previous : 0;
        const int latest = (history_write_idx + GLUCOSE_HISTORY_SIZE - 1) % GLUCOSE_HISTORY_SIZE;
        history_buf[latest].delta = current_delta;
    }
    ever_received = true;
    failure_count = 0;
    last_success_ms = now;
    xSemaphoreGive(data_mutex);
    return true;
}

static bool demo_generate() {
    const unsigned long now = millis();
    demo_last_update_ms = now;
    demo_value += (int)random(-4, 5);
    demo_value = constrain(demo_value, 80, 100);
    GlucoseReading r = {};
    int delta = has_prev_reading ? demo_value - prev_glucose : 0;
    r.trend = delta > 1 ? TREND_RISING : (delta < -1 ? TREND_FALLING : TREND_FLAT);
    r.glucose = demo_value;
    r.received_at_ms = now;
    r.force_mode = -1;
    r.timestamp = now / 1000;
    r.valid = true;
    commit_reading(r);
    failure_count = 0;
    ever_received = true;
    last_response_code = 200;
    last_success_ms = now;
    set_last_response("demo mode");
    return true;
}

// Run the configured fetch (network task only)
static bool do_fetch() {
    AppConfig& cfg = config_get();
    if (cfg.data_source == 1) {
        return dexcom_fetch_glucose();
    }
    if (cfg.data_source == 2) return demo_generate();
    if (cfg.data_source == 4) return fetch_nightscout_reading();
    // Source 3 is reserved for Libre; never send its settings to Custom URL.
    if (cfg.data_source == 3) {
        set_last_response("Libre is not available in this firmware branch");
        last_response_code = 0;
        ++failure_count;
        return false;
    }
    return generic_fetch();
}

void http_reset_source() {
    // Caller owns the network gate, so no older request can publish afterwards.
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    memset(&current_reading, 0, sizeof(current_reading));
    current_reading.force_mode = -1;
    last_poll_ms = 0;
    last_success_ms = 0;
    failure_count = 0;
    last_response_code = 0;
    last_response_body[0] = '\0';
    ever_received = false;
    dexcom_session_id[0] = '\0';
    dexcom_session_time_ms = 0;
    history_write_idx = history_count = 0;
    has_prev_reading = false;
    current_delta = prev_glucose = 0;
    last_recorded_timestamp = 0;
    sensor_age_active = false;
    sensor_age_ms = 0;
    sensor_received_ms = 0;
    delta_available = config_get().data_source != 4;
    demo_last_update_ms = 0;
    demo_value = 90;
    // Cancel a queued test when its settings were replaced.
    if (force_pending_id) {
        force_done_id = force_pending_id;
        force_pending_id = 0;
        force_result = false;
        xSemaphoreGive(force_done_sem);
    }
    xSemaphoreGive(data_mutex);
}

void http_init() {
    if (!data_mutex) data_mutex = xSemaphoreCreateMutex();
    if (!force_done_sem) force_done_sem = xSemaphoreCreateBinary();
    if (!force_caller_mutex) force_caller_mutex = xSemaphoreCreateMutex();
    if (!data_mutex || !force_done_sem || !force_caller_mutex) {
        Serial.println("[HTTP] Cannot allocate synchronization state");
        ESP.restart();
        return;
    }
    http_reset_source();
}

void http_poll_tick() {
    if (http_paused) return;
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    const uint32_t request_id = force_pending_id;
    force_pending_id = 0;
    xSemaphoreGive(data_mutex);
    const AppConfig& cfg = config_get();
    if (request_id) {
        bool result = false;
        if (cfg.data_source == 2 || (wifi_is_connected() && config_has_server())) {
            last_poll_ms = millis();
            result = do_fetch();
        } else {
            last_response_code = 0;
            set_last_response("Wi-Fi or data source is not configured");
        }
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        force_done_id = request_id;
        force_result = result;
        xSemaphoreGive(data_mutex);
        xSemaphoreGive(force_done_sem);
        return;
    }
    if (cfg.data_source == 2) {
        if (!demo_last_update_ms || millis() - demo_last_update_ms >= DEMO_UPDATE_MS) {
            demo_generate();
        }
        return;
    }
    if (!wifi_is_connected() || !config_has_server()) return;
    unsigned long interval_ms = max(15, cfg.poll_interval_sec) * 1000UL;
    if (last_poll_ms && millis() - last_poll_ms < interval_ms) return;
    last_poll_ms = millis();
    do_fetch();
}

GlucoseReading http_get_reading() {
    GlucoseReading r;
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    r = current_reading;
    xSemaphoreGive(data_mutex);
    return r;
}

int http_get_failure_count() {
    return failure_count;
}

int http_get_last_response_code() {
    return last_response_code;
}

void http_get_last_response_body(char* out, size_t out_len) {
    if (out == NULL || out_len == 0) return;
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    strncpy(out, last_response_body, out_len - 1);
    out[out_len - 1] = '\0';
    xSemaphoreGive(data_mutex);
}

bool http_has_ever_received() {
    return ever_received;
}

unsigned long http_time_since_last_reading() {
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    unsigned long age = ULONG_MAX;
    if (ever_received) {
        const unsigned long now = millis();
        uint64_t value = sensor_age_active
            ? static_cast<uint64_t>(sensor_age_ms) + static_cast<uint32_t>(now - sensor_received_ms)
            : static_cast<uint32_t>(now - last_success_ms);
        age = value >= UINT32_MAX ? UINT32_MAX : static_cast<unsigned long>(value);
        if (sensor_age_active) {
            // Accumulate while the display polls age, including millis rollover.
            // A long offline period must never turn an old reading fresh again.
            sensor_age_ms = static_cast<uint32_t>(age);
            sensor_received_ms = now;
        }
    }
    xSemaphoreGive(data_mutex);
    return age;
}

int http_get_delta() {
    return current_delta;
}

bool http_has_delta() {
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    const bool available = delta_available;
    xSemaphoreGive(data_mutex);
    return available;
}

static void set_force_error(const char* error) {
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    snprintf(force_error, sizeof(force_error), "%s", error);
    xSemaphoreGive(data_mutex);
}

void http_get_force_error(char* out, size_t size) {
    if (!out || !size) return;
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    snprintf(out, size, "%s", force_error);
    xSemaphoreGive(data_mutex);
}

bool http_force_fetch(unsigned long timeout_ms) {
    if (http_paused || xSemaphoreTake(force_caller_mutex, 0) != pdTRUE) {
        set_force_error("Data request busy or paused. Retry in a moment.");
        return false;
    }
    set_force_error("");
    xSemaphoreTake(force_done_sem, 0);
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    if (++force_next_id == 0) ++force_next_id;
    const uint32_t request_id = force_next_id;
    force_pending_id = request_id;
    xSemaphoreGive(data_mutex);
    const unsigned long started = millis();
    bool ok = false;
    bool completed = false;
    while (millis() - started < timeout_ms) {
        const unsigned long elapsed = millis() - started;
        if (elapsed >= timeout_ms) break;
        if (xSemaphoreTake(force_done_sem, pdMS_TO_TICKS(timeout_ms - elapsed)) != pdTRUE) break;
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        const bool ours = force_done_id == request_id;
        const bool result = force_result;
        xSemaphoreGive(data_mutex);
        if (ours) { ok = result; completed = true; break; }
    }
    if (!completed) set_force_error("Data request timed out. Retry in a moment.");
    xSemaphoreGive(force_caller_mutex);
    return ok;
}

int http_get_history(GlucoseHistoryEntry* out, int max_count) {
    xSemaphoreTake(data_mutex, portMAX_DELAY);

    if (history_count == 0) {
        xSemaphoreGive(data_mutex);
        return 0;
    }

    int count = min(max_count, history_count);
    // Read from oldest to newest
    int start;
    if (history_count < GLUCOSE_HISTORY_SIZE) {
        start = 0;
    } else {
        start = history_write_idx; // oldest entry
    }

    for (int i = 0; i < count; i++) {
        int idx = (start + (history_count - count) + i) % GLUCOSE_HISTORY_SIZE;
        out[i] = history_buf[idx];
    }

    xSemaphoreGive(data_mutex);
    return count;
}

void http_set_paused(bool paused) {
    http_paused = paused;
}

bool http_is_paused() {
    return http_paused;
}
