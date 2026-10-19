#include "http_client.h"
#include "time_engine.h"
#include "glucose_freshness.h"
#include "trend_mapping.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "libre_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>

// Dexcom Share constants
#define DEXCOM_APP_ID "d89443d2-327c-4a6f-89e5-496bbb0317db"
#define DEXCOM_US_BASE "https://share2.dexcom.com/ShareWebServices/Services"
#define DEXCOM_OUS_BASE "https://shareous1.dexcom.com/ShareWebServices/Services"
#define DEXCOM_AUTH_PATH "/General/AuthenticatePublisherAccount"
#define DEXCOM_LOGIN_PATH "/General/LoginPublisherAccountById"
#define DEXCOM_GLUCOSE_PATH "/Publisher/ReadPublisherLatestGlucoseValues"
#define DEXCOM_NULL_SESSION "00000000-0000-0000-0000-000000000000"
#define DEXCOM_SESSION_LIFETIME_MS (3600000UL) // re-auth every hour

struct AccountState {
    GlucoseReading current_reading = {};
    int failure_count = 0;
    int last_response_code = 0;
    char last_response_body[512] = "";
    bool ever_received = false;
    unsigned long last_poll_ms = 0;
    // Same wraparound/pre-boot semantics as GlucoseReading::received_at_ms.
    // ever_received distinguishes an empty history; timestamp zero is valid.
    unsigned long last_success_ms = 0;


    // Delta tracking
    int prev_glucose = 0;
    int current_delta = 0;
    bool has_prev_reading = false;
    unsigned long last_recorded_timestamp = 0;

    // History circular buffer
    GlucoseHistoryEntry history_buf[GLUCOSE_HISTORY_SIZE] = {};
    int history_write_idx = 0;
    int history_count = 0;

    // Dexcom session state
    char dexcom_session_id[64] = "";
    unsigned long dexcom_session_time_ms = 0;
};
static AccountState accounts[CGM_MAX_ACCOUNTS];
// Only this working copy is touched during network IO. Publication is atomic
// under the config lock; a changed generation discards an in-flight response.
static std::mutex fetch_mutex;
static bool http_paused = false;
static int selected_account = 0;
static unsigned generations[CGM_MAX_ACCOUNTS] = {};
static int next_poll_account = 0;

// Demo mode: synthetic glucose that gently wanders in a normal 80-100 range.
// Used for recording videos / demos without a real CGM connection.
#define DEMO_UPDATE_MS 5000
static unsigned long demo_last_update_ms = 0;
static int demo_value = 90;

// Record a glucose value to history and update delta.
// reading_timestamp is the CGM timestamp (epoch seconds) so we can skip
// duplicate readings that arrive when we poll faster than the CGM updates.
static void record_reading(AccountState& s, int glucose, unsigned long reading_timestamp) {
    // Skip duplicate readings — same CGM timestamp means same reading
    if (reading_timestamp > 0 && reading_timestamp == s.last_recorded_timestamp) {
        return;
    }
    s.last_recorded_timestamp = reading_timestamp;

    if (s.has_prev_reading) {
        s.current_delta = glucose - s.prev_glucose;
    } else {
        s.current_delta = 0;
    }
    s.prev_glucose = glucose;
    s.has_prev_reading = true;

    // Add to history buffer
    s.history_buf[s.history_write_idx].glucose = glucose;
    s.history_buf[s.history_write_idx].delta = s.current_delta;
    s.history_buf[s.history_write_idx].timestamp = millis();
    s.history_write_idx = (s.history_write_idx + 1) % GLUCOSE_HISTORY_SIZE;
    if (s.history_count < GLUCOSE_HISTORY_SIZE) {
        s.history_count++;
    }

    Serial.printf("[HTTP] Delta: %+d (prev: %d, now: %d)\n", s.current_delta, s.prev_glucose - s.current_delta, glucose);
}

// Helper: POST JSON to Dexcom endpoint, return response string
static String dexcom_post(const char* url, const String& body, int& httpCode) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    HTTPClient http;
    if (!http.begin(client, url)) {
        httpCode = -1;
        return "";
    }

    http.setTimeout(15000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    httpCode = http.POST(body);
    String response = http.getString();
    http.end();
    return response;
}

// Dexcom Share: two-step authenticate and get session ID
static bool dexcom_login(AccountState& s, const DexcomAccount& cfg) {
    const char* base = cfg.us ? DEXCOM_US_BASE : DEXCOM_OUS_BASE;

    // Step 1: AuthenticatePublisherAccount (get account ID)
    JsonDocument authDoc;
    authDoc["accountName"] = cfg.username;
    authDoc["password"] = cfg.password;
    authDoc["applicationId"] = DEXCOM_APP_ID;
    String authBody;
    serializeJson(authDoc, authBody);

    Serial.printf("[DEXCOM] Authenticating (%s)...\n", cfg.us ? "US" : "OUS");

    char auth_url[256];
    snprintf(auth_url, sizeof(auth_url), "%s%s", base, DEXCOM_AUTH_PATH);

    int authCode;
    String authResp = dexcom_post(auth_url, authBody, authCode);
    snprintf(s.last_response_body, sizeof(s.last_response_body), "Dexcom authentication HTTP %d", authCode);
    s.last_response_code = authCode;

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
    loginDoc["password"] = cfg.password;
    loginDoc["applicationId"] = DEXCOM_APP_ID;
    String loginBody;
    serializeJson(loginDoc, loginBody);

    char login_url[256];
    snprintf(login_url, sizeof(login_url), "%s%s", base, DEXCOM_LOGIN_PATH);

    int loginCode;
    String loginResp = dexcom_post(login_url, loginBody, loginCode);
    snprintf(s.last_response_body, sizeof(s.last_response_body), "Dexcom login HTTP %d", loginCode);
    s.last_response_code = loginCode;

    Serial.printf("[DEXCOM] Auth step 2: HTTP %d\n", loginCode);

    if (loginCode == HTTP_CODE_OK) {
        loginResp.trim();
        loginResp.replace("\"", "");

        // Check for null session (means Share not enabled or no followers)
        if (loginResp == DEXCOM_NULL_SESSION || loginResp.length() < 10) {
            Serial.println("[DEXCOM] Got null session! Dexcom Share may not be enabled.");
            Serial.println("[DEXCOM] Enable Share in Dexcom app: Settings > Share > enable sharing");
            strncpy(s.last_response_body, "Null session - enable Dexcom Share in app", sizeof(s.last_response_body) - 1);
            return false;
        }

        strncpy(s.dexcom_session_id, loginResp.c_str(), sizeof(s.dexcom_session_id) - 1);
        s.dexcom_session_time_ms = millis();
        Serial.println("[DEXCOM] Login OK");
        return true;
    }

    Serial.printf("[DEXCOM] Login failed: HTTP %d\n", loginCode);
    return false;
}

// Dexcom Share: fetch latest glucose reading
static bool dexcom_fetch_glucose(AccountState& s, const DexcomAccount& cfg) {

    // Check if session needs refresh
    if (strlen(s.dexcom_session_id) == 0 ||
        (millis() - s.dexcom_session_time_ms > DEXCOM_SESSION_LIFETIME_MS)) {
        if (!dexcom_login(s, cfg)) {
            s.failure_count++;
            return false;
        }
    }

    const char* base = cfg.us ? DEXCOM_US_BASE : DEXCOM_OUS_BASE;
    char url[384];
    snprintf(url, sizeof(url), "%s%s?sessionId=%s&minutes=10&maxCount=1",
             base, DEXCOM_GLUCOSE_PATH, s.dexcom_session_id);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    if (!http.begin(client, url)) {
        Serial.println("[DEXCOM] Fetch: failed to begin");
        s.failure_count++;
        return false;
    }

    http.setTimeout(15000);
    http.addHeader("Accept", "application/json");

    int httpCode = http.POST(""); // Dexcom requires POST even for reads
    s.last_response_code = httpCode;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        strncpy(s.last_response_body, payload.c_str(), sizeof(s.last_response_body) - 1);
        s.last_response_body[sizeof(s.last_response_body) - 1] = '\0';

        // Parse JSON array response
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[DEXCOM] JSON parse error: %s\n", err.c_str());
            s.failure_count++;
            http.end();
            return false;
        }

        // Response is an array, get first element
        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() == 0) {
            Serial.println("[DEXCOM] Empty glucose array");
            s.failure_count++;
            http.end();
            return false;
        }

        JsonObject reading = arr[0];

        s.current_reading.glucose = reading["Value"] | 0;
        s.current_reading.received_at_ms = millis();
        s.current_reading.force_mode = -1;
        s.current_reading.message[0] = '\0';

        // Parse trend - can be string or number
        if (reading["Trend"].is<int>()) {
            s.current_reading.trend = parse_trend_number(reading["Trend"].as<int>());
        } else if (reading["Trend"].is<const char*>()) {
            s.current_reading.trend = parse_trend(reading["Trend"] | "Unknown");
        } else {
            s.current_reading.trend = TREND_UNKNOWN;
        }

        // Parse timestamp from "Date(1234567890000)" or "WT" field
        s.current_reading.timestamp = 0;
        const char* wt = reading["WT"] | reading["ST"] | "";
        if (strlen(wt) > 0) {
            // Extract epoch ms from "Date(1234567890000)" or "/Date(1234567890000)/"
            const char* start = strchr(wt, '(');
            if (start) {
                s.current_reading.timestamp = (unsigned long)(strtoull(start + 1, NULL, 10) / 1000ULL);
            }
        }

        s.current_reading.valid = (s.current_reading.glucose > 0);

        if (s.current_reading.valid) {
            const bool synced = time_is_network_synced();
            s.current_reading.received_at_ms = glucose_received_at(
                s.current_reading.timestamp, s.last_recorded_timestamp, s.ever_received,
                s.last_success_ms, millis(), synced, synced ? uint32_t(time(nullptr)) : 0);
            record_reading(s, s.current_reading.glucose, s.current_reading.timestamp);
            s.failure_count = 0;
            s.ever_received = true;
            s.last_success_ms = s.current_reading.received_at_ms;
            Serial.printf("[DEXCOM] Glucose: %d, Trend: %s\n",
                          s.current_reading.glucose,
                          TREND_NAMES[s.current_reading.trend]);
        } else {
            s.failure_count++;
        }

        http.end();
        return s.current_reading.valid;
    }

    // Session expired? Try re-login
    if (httpCode == 500) {
        Serial.println("[DEXCOM] Session expired, re-authenticating");
        s.dexcom_session_id[0] = '\0';
    }

    String resp = http.getString();
    strncpy(s.last_response_body, resp.c_str(), sizeof(s.last_response_body) - 1);
    Serial.printf("[DEXCOM] Fetch failed: HTTP %d\n", httpCode);
    s.failure_count++;
    http.end();
    return false;
}

// FreeStyle Libre: fetch latest reading via LibreLinkUp
static bool libre_fetch_glucose(AccountState& s) {
    LibreReading reading;
    bool attempted = false;
    bool ok = libre_fetch(reading, &attempted);

    s.last_response_code = libre_last_http_code();
    strncpy(s.last_response_body, libre_last_message(), sizeof(s.last_response_body) - 1);
    s.last_response_body[sizeof(s.last_response_body) - 1] = '\0';

    if (!ok) {
        if (attempted) s.failure_count++;
        return false;
    }

    // Date the reading by its sensor timestamp, not by when we polled, so
    // staleness reflects the reading's real age.
    unsigned long sensor_ms = millis() - reading.age_sec * 1000UL;

    s.current_reading.glucose = reading.glucose;
    s.current_reading.trend = reading.trend;
    s.current_reading.timestamp = reading.timestamp;
    s.current_reading.received_at_ms = sensor_ms;
    s.current_reading.force_mode = -1;
    s.current_reading.message[0] = '\0';
    s.current_reading.valid = true;

    record_reading(s, s.current_reading.glucose, s.current_reading.timestamp);
    s.failure_count = 0;
    s.ever_received = true;
    s.last_success_ms = sensor_ms;
    Serial.printf("[LIBRE] Glucose: %d, Trend: %s\n",
                  s.current_reading.glucose,
                  TREND_NAMES[s.current_reading.trend]);
    return true;
}

// Generic URL fetch (original behavior)
static void generic_fetch(AccountState& s) {
    AppConfig& cfg = config_get();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    Serial.printf("[HTTP] Polling: %s\n", cfg.server_url);

    if (!http.begin(client, cfg.server_url)) {
        Serial.println("[HTTP] Failed to begin connection");
        s.failure_count++;
        s.last_response_code = -1;
        return;
    }

    http.setTimeout(10000);
    http.addHeader("Accept", "application/json");

    if (strlen(cfg.auth_token) > 0) {
        char auth_header[280];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", cfg.auth_token);
        http.addHeader("Authorization", auth_header);
    }

    int httpCode = http.GET();
    s.last_response_code = httpCode;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        strncpy(s.last_response_body, payload.c_str(), sizeof(s.last_response_body) - 1);
        s.last_response_body[sizeof(s.last_response_body) - 1] = '\0';

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[HTTP] JSON parse error: %s\n", err.c_str());
            s.failure_count++;
        } else {
            s.current_reading.glucose = doc["glucose"] | 0;
            s.current_reading.timestamp = doc["timestamp"] | 0UL;
            s.current_reading.received_at_ms = millis();
            s.current_reading.force_mode = doc["force_mode"] | -1;
            s.current_reading.valid = (s.current_reading.glucose > 0);

            const char* trend_str = doc["trend"] | "Unknown";
            s.current_reading.trend = parse_trend(trend_str);

            const char* msg = doc["message"] | "";
            strncpy(s.current_reading.message, msg, sizeof(s.current_reading.message) - 1);
            s.current_reading.message[sizeof(s.current_reading.message) - 1] = '\0';

            if (s.current_reading.valid) {
                const bool synced = time_is_network_synced();
                s.current_reading.received_at_ms = glucose_received_at(
                    s.current_reading.timestamp, s.last_recorded_timestamp, s.ever_received,
                    s.last_success_ms, millis(), synced, synced ? uint32_t(time(nullptr)) : 0);
                record_reading(s, s.current_reading.glucose, s.current_reading.timestamp);
                s.failure_count = 0;
                s.ever_received = true;
                s.last_success_ms = s.current_reading.received_at_ms;
                Serial.printf("[HTTP] Glucose: %d, Trend: %s\n",
                              s.current_reading.glucose,
                              TREND_NAMES[s.current_reading.trend]);
            } else {
                s.failure_count++;
                Serial.println("[HTTP] Invalid glucose value");
            }
        }
    } else {
        Serial.printf("[HTTP] Error: %d\n", httpCode);
        snprintf(s.last_response_body, sizeof(s.last_response_body), "HTTP %d", httpCode);
        s.failure_count++;
    }

    http.end();
}

// Demo mode: generate a synthetic in-range reading. Wanders gently via a
// small random walk clamped to 80-100 so the display looks "alive" on camera.
static void demo_generate(AccountState& s) {
    unsigned long now = millis();

    // First reading is produced immediately; after that, update on an interval
    // slow enough to look like a real CGM but fast enough to visibly fluctuate.
    if (demo_last_update_ms != 0 && (now - demo_last_update_ms < DEMO_UPDATE_MS)) {
        return;
    }
    demo_last_update_ms = now;

    demo_value += (int)random(-4, 5);   // -4..+4
    if (demo_value < 80) demo_value = 80;
    if (demo_value > 100) demo_value = 100;

    // Derive a plausible trend arrow from the change (before record_reading
    // overwrites s.prev_glucose).
    int delta = s.has_prev_reading ? (demo_value - s.prev_glucose) : 0;
    if (delta > 1)       s.current_reading.trend = TREND_RISING;
    else if (delta < -1) s.current_reading.trend = TREND_FALLING;
    else                 s.current_reading.trend = TREND_FLAT;

    s.current_reading.glucose = demo_value;
    s.current_reading.received_at_ms = now;
    s.current_reading.force_mode = -1;
    s.current_reading.message[0] = '\0';
    s.current_reading.timestamp = now / 1000;   // synthetic, unique per update
    s.current_reading.valid = true;

    record_reading(s, demo_value, s.current_reading.timestamp);
    s.failure_count = 0;
    s.ever_received = true;
    s.last_response_code = 200;
    s.last_success_ms = now;
    strncpy(s.last_response_body, "demo mode", sizeof(s.last_response_body) - 1);
}

// Callers hold config_libre_mutex() for configuration and published state.
static DexcomAccount credentials(int index) {
    const auto& cfg = config_get();
    if (index > 0) return cfg.extra_dexcom[index - 1];
    DexcomAccount a = {};
    strcpy(a.label, cfg.dexcom_label);
    strcpy(a.username, cfg.dexcom_username);
    strcpy(a.password, cfg.dexcom_password);
    a.us = cfg.dexcom_us;
    return a;
}

bool http_account_enabled(int index) {
    LibreConfigLock lock(config_libre_mutex());
    if (index < 0 || index >= CGM_MAX_ACCOUNTS) return false;
    if (index == 0) return true;
    if (config_get().data_source != 1) return false;
    const auto& a = config_get().extra_dexcom[index - 1];
    return a.username[0] && a.password[0];
}

int http_account_count() {
    int count = 0;
    for (int i = 0; i < CGM_MAX_ACCOUNTS; ++i) count += http_account_enabled(i);
    return count;
}

bool http_account_label(int index, char* out, size_t size) {
    LibreConfigLock lock(config_libre_mutex());
    const auto a = credentials(index >= 0 && index < CGM_MAX_ACCOUNTS ? index : 0);
    const bool custom = a.label[0] && config_get().data_source == 1;
    if (custom) snprintf(out, size, "%s", a.label);
    else snprintf(out, size, "CGM%d", index + 1);
    return custom;
}

int http_selected_account() {
    LibreConfigLock lock(config_libre_mutex());
    if (!http_account_enabled(selected_account)) selected_account = 0;
    return selected_account;
}

void http_select_account(int index) {
    LibreConfigLock lock(config_libre_mutex());
    selected_account = http_account_enabled(index) ? index : 0;
}

void http_reset_account(int index) {
    LibreConfigLock lock(config_libre_mutex());
    if (index < 0 || index >= CGM_MAX_ACCOUNTS) return;
    accounts[index] = AccountState{};
    accounts[index].current_reading.force_mode = -1;
    ++generations[index];
}

void http_clear_readings() { http_reset_account(0); }

void http_init() {
    for (int i = 0; i < CGM_MAX_ACCOUNTS; ++i) http_reset_account(i);
    selected_account = next_poll_account = 0;
    libre_reset_session();
    demo_last_update_ms = 0;
    demo_value = 90;
}

static bool fetch_account(int index, bool force) {
    // Test Connection and regular polling share one transport, with no extra
    // TLS buffers and no blocking wait in the display loop.
    std::unique_lock<std::mutex> transport(fetch_mutex, std::try_to_lock);
    if (!transport.owns_lock()) return false;
    static AccountState working;
    DexcomAccount identity;
    unsigned generation;
    int source;
    {
        LibreConfigLock lock(config_libre_mutex());
        if (http_paused || !http_account_enabled(index)) return false;
        source = config_get().data_source;
        if (source != 2 && (!wifi_is_connected() ||
            (index == 0 && !config_has_server()))) return false;
        auto& saved = accounts[index];
        const unsigned long interval = max(15, config_get().poll_interval_sec) * 1000UL;
        if (!force && source != 2 && saved.last_poll_ms != 0 &&
            millis() - saved.last_poll_ms < interval) return false;
        saved.last_poll_ms = millis();
        working = saved;
        identity = credentials(index);
        generation = generations[index];
    }
    bool ok = false;
    if (source == 2) {
        if (force) demo_last_update_ms = 0;
        demo_generate(working);
        ok = working.current_reading.valid;
    } else if (source == 1) ok = dexcom_fetch_glucose(working, identity);
    else if (source == 3) ok = libre_fetch_glucose(working);
    else { generic_fetch(working); ok = working.failure_count == 0 && working.current_reading.valid; }
    {
        LibreConfigLock lock(config_libre_mutex());
        if (generation != generations[index] || source != config_get().data_source) return false;
        accounts[index] = working;
    }
    return ok;
}

void http_loop() {
    // Round-robin one account per loop, including failures. Each account keeps
    // its own poll deadline; a failing login cannot starve the other accounts.
    for (int n = 0; n < CGM_MAX_ACCOUNTS; ++n) {
        const int index = next_poll_account;
        next_poll_account = (next_poll_account + 1) % CGM_MAX_ACCOUNTS;
        if (http_account_enabled(index)) { fetch_account(index, false); break; }
    }
}

GlucoseReading http_get_reading() {
    LibreConfigLock lock(config_libre_mutex());
    return accounts[http_selected_account()].current_reading;
}

int http_get_failure_count() {
    LibreConfigLock lock(config_libre_mutex());
    return accounts[http_selected_account()].failure_count;
}

int http_get_last_response_code() {
    LibreConfigLock lock(config_libre_mutex());
    return accounts[http_selected_account()].last_response_code;
}

const char* http_get_last_response_body() {
    return accounts[http_selected_account()].last_response_body;
}

bool http_has_ever_received() {
    LibreConfigLock lock(config_libre_mutex());
    return accounts[http_selected_account()].ever_received;
}

unsigned long http_time_since_last_reading() {
    LibreConfigLock lock(config_libre_mutex());
    const auto& s = accounts[http_selected_account()];
    return s.ever_received ? millis() - s.last_success_ms : ULONG_MAX;
}

int http_get_delta() {
    LibreConfigLock lock(config_libre_mutex());
    return accounts[http_selected_account()].current_delta;
}

bool http_force_fetch() { return fetch_account(http_selected_account(), true); }

bool http_test_account(int index, GlucoseReading& reading, int& code) {
    const bool ok = fetch_account(index, true);
    LibreConfigLock lock(config_libre_mutex());
    if (!http_account_enabled(index)) { code = 400; return false; }
    reading = accounts[index].current_reading;
    code = accounts[index].last_response_code;
    return ok;
}

// Find fresh out-of-range readings across ALL accounts, even off-screen.
int http_find_alert(int low, int high, int start) {
    LibreConfigLock lock(config_libre_mutex());
    const auto& cfg = config_get();
    for (int n = 0; n < CGM_MAX_ACCOUNTS; ++n) {
        const int index = (start + n) % CGM_MAX_ACCOUNTS;
        if (!http_account_enabled(index)) continue;
        const auto& s = accounts[index];
        if (!s.current_reading.valid) continue;
        if (cfg.data_source != 2 && (s.failure_count >= 5 ||
            millis() - s.last_success_ms >= (unsigned long)cfg.stale_timeout_min * 60000UL)) continue;
        if (s.current_reading.glucose < low || s.current_reading.glucose > high) return index;
    }
    return -1;
}

int http_get_history(GlucoseHistoryEntry* out, int max_count) {
    LibreConfigLock lock(config_libre_mutex());
    const auto& s = accounts[http_selected_account()];
    const int count = min(max_count, s.history_count);
    const int start = s.history_count < GLUCOSE_HISTORY_SIZE ? 0 : s.history_write_idx;
    for (int i = 0; i < count; ++i)
        out[i] = s.history_buf[(start + s.history_count - count + i) % GLUCOSE_HISTORY_SIZE];
    return count;
}

void http_set_paused(bool paused) {
    LibreConfigLock lock(config_libre_mutex());
    http_paused = paused;
}

bool http_is_paused() {
    LibreConfigLock lock(config_libre_mutex());
    return http_paused;
}
