#include "http_client.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>

// Dexcom Share constants
#define DEXCOM_APP_ID "d89443d2-327c-4a6f-89e5-496bbb0317db"
#define DEXCOM_US_BASE "https://share2.dexcom.com/ShareWebServices/Services"
#define DEXCOM_OUS_BASE "https://shareous1.dexcom.com/ShareWebServices/Services"
#define DEXCOM_AUTH_PATH "/General/AuthenticatePublisherAccount"
#define DEXCOM_LOGIN_PATH "/General/LoginPublisherAccountById"
#define DEXCOM_GLUCOSE_PATH "/Publisher/ReadPublisherLatestGlucoseValues"
#define DEXCOM_NULL_SESSION "00000000-0000-0000-0000-000000000000"
#define DEXCOM_SESSION_LIFETIME_MS (3600000UL) // re-auth every hour

static GlucoseReading current_reading;
static int failure_count = 0;
static int last_response_code = 0;
static char last_response_body[512] = "";
static bool ever_received = false;
static unsigned long last_poll_ms = 0;
static unsigned long last_success_ms = 0;

// Delta tracking
static int prev_glucose = 0;
static int current_delta = 0;
static bool has_prev_reading = false;

// History circular buffer
static GlucoseHistoryEntry history_buf[GLUCOSE_HISTORY_SIZE];
static int history_write_idx = 0;
static int history_count = 0;

// Dexcom session state
static char dexcom_session_id[64] = "";
static unsigned long dexcom_session_time_ms = 0;

// Record a glucose value to history and update delta
static void record_reading(int glucose) {
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

    Serial.printf("[DEXCOM] Auth as '%s' (%s)...\n", cfg.dexcom_username, cfg.dexcom_us ? "US" : "OUS");

    char auth_url[256];
    snprintf(auth_url, sizeof(auth_url), "%s%s", base, DEXCOM_AUTH_PATH);

    int authCode;
    String authResp = dexcom_post(auth_url, authBody, authCode);
    strncpy(last_response_body, authResp.c_str(), sizeof(last_response_body) - 1);
    last_response_code = authCode;

    Serial.printf("[DEXCOM] Auth step 1: HTTP %d, body: %.60s\n", authCode, authResp.c_str());

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
    strncpy(last_response_body, loginResp.c_str(), sizeof(last_response_body) - 1);
    last_response_code = loginCode;

    Serial.printf("[DEXCOM] Auth step 2: HTTP %d, body: %.60s\n", loginCode, loginResp.c_str());

    if (loginCode == HTTP_CODE_OK) {
        loginResp.trim();
        loginResp.replace("\"", "");

        // Check for null session (means Share not enabled or no followers)
        if (loginResp == DEXCOM_NULL_SESSION || loginResp.length() < 10) {
            Serial.println("[DEXCOM] Got null session! Dexcom Share may not be enabled.");
            Serial.println("[DEXCOM] Enable Share in Dexcom app: Settings > Share > enable sharing");
            strncpy(last_response_body, "Null session - enable Dexcom Share in app", sizeof(last_response_body) - 1);
            return false;
        }

        strncpy(dexcom_session_id, loginResp.c_str(), sizeof(dexcom_session_id) - 1);
        dexcom_session_time_ms = millis();
        Serial.printf("[DEXCOM] Login OK, session: %.8s...\n", dexcom_session_id);
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

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    if (!http.begin(client, url)) {
        Serial.println("[DEXCOM] Fetch: failed to begin");
        failure_count++;
        return false;
    }

    http.setTimeout(15000);
    http.addHeader("Accept", "application/json");

    int httpCode = http.POST(""); // Dexcom requires POST even for reads
    last_response_code = httpCode;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        strncpy(last_response_body, payload.c_str(), sizeof(last_response_body) - 1);
        last_response_body[sizeof(last_response_body) - 1] = '\0';

        // Parse JSON array response
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[DEXCOM] JSON parse error: %s\n", err.c_str());
            failure_count++;
            http.end();
            return false;
        }

        // Response is an array, get first element
        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() == 0) {
            Serial.println("[DEXCOM] Empty glucose array");
            failure_count++;
            http.end();
            return false;
        }

        JsonObject reading = arr[0];

        current_reading.glucose = reading["Value"] | 0;
        current_reading.received_at_ms = millis();
        current_reading.force_mode = -1;
        current_reading.message[0] = '\0';

        // Parse trend - can be string or number
        if (reading["Trend"].is<int>()) {
            current_reading.trend = parse_trend_number(reading["Trend"].as<int>());
        } else if (reading["Trend"].is<const char*>()) {
            current_reading.trend = parse_trend(reading["Trend"] | "Unknown");
        } else {
            current_reading.trend = TREND_UNKNOWN;
        }

        // Parse timestamp from "Date(1234567890000)" or "WT" field
        const char* wt = reading["WT"] | reading["ST"] | "";
        if (strlen(wt) > 0) {
            // Extract epoch ms from "Date(1234567890000)" or "/Date(1234567890000)/"
            const char* start = strchr(wt, '(');
            if (start) {
                current_reading.timestamp = strtoul(start + 1, NULL, 10) / 1000;
            }
        }

        current_reading.valid = (current_reading.glucose > 0);

        if (current_reading.valid) {
            record_reading(current_reading.glucose);
            failure_count = 0;
            ever_received = true;
            last_success_ms = millis();
            Serial.printf("[DEXCOM] Glucose: %d, Trend: %s\n",
                          current_reading.glucose,
                          TREND_NAMES[current_reading.trend]);
        } else {
            failure_count++;
        }

        http.end();
        return current_reading.valid;
    }

    // Session expired? Try re-login
    if (httpCode == 500) {
        Serial.println("[DEXCOM] Session expired, re-authenticating");
        dexcom_session_id[0] = '\0';
    }

    String resp = http.getString();
    strncpy(last_response_body, resp.c_str(), sizeof(last_response_body) - 1);
    Serial.printf("[DEXCOM] Fetch failed: HTTP %d\n", httpCode);
    failure_count++;
    http.end();
    return false;
}

// Generic URL fetch (original behavior)
static void generic_fetch() {
    AppConfig& cfg = config_get();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    Serial.printf("[HTTP] Polling: %s\n", cfg.server_url);

    if (!http.begin(client, cfg.server_url)) {
        Serial.println("[HTTP] Failed to begin connection");
        failure_count++;
        last_response_code = -1;
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
    last_response_code = httpCode;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        strncpy(last_response_body, payload.c_str(), sizeof(last_response_body) - 1);
        last_response_body[sizeof(last_response_body) - 1] = '\0';

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("[HTTP] JSON parse error: %s\n", err.c_str());
            failure_count++;
        } else {
            current_reading.glucose = doc["glucose"] | 0;
            current_reading.timestamp = doc["timestamp"] | 0UL;
            current_reading.received_at_ms = millis();
            current_reading.force_mode = doc["force_mode"] | -1;
            current_reading.valid = (current_reading.glucose > 0);

            const char* trend_str = doc["trend"] | "Unknown";
            current_reading.trend = parse_trend(trend_str);

            const char* msg = doc["message"] | "";
            strncpy(current_reading.message, msg, sizeof(current_reading.message) - 1);
            current_reading.message[sizeof(current_reading.message) - 1] = '\0';

            if (current_reading.valid) {
                record_reading(current_reading.glucose);
                failure_count = 0;
                ever_received = true;
                last_success_ms = millis();
                Serial.printf("[HTTP] Glucose: %d, Trend: %s\n",
                              current_reading.glucose,
                              TREND_NAMES[current_reading.trend]);
            } else {
                failure_count++;
                Serial.println("[HTTP] Invalid glucose value");
            }
        }
    } else {
        Serial.printf("[HTTP] Error: %d\n", httpCode);
        snprintf(last_response_body, sizeof(last_response_body), "HTTP %d", httpCode);
        failure_count++;
    }

    http.end();
}

void http_init() {
    memset(&current_reading, 0, sizeof(GlucoseReading));
    current_reading.valid = false;
    current_reading.force_mode = -1;
    last_poll_ms = 0;
    last_success_ms = 0;
    dexcom_session_id[0] = '\0';

    // Reset history
    history_write_idx = 0;
    history_count = 0;
    has_prev_reading = false;
    current_delta = 0;
    prev_glucose = 0;
}

void http_loop() {
    if (!wifi_is_connected()) return;
    if (!config_has_server()) return;

    AppConfig& cfg = config_get();

    unsigned long interval_ms = max(15, cfg.poll_interval_sec) * 1000UL;

    if (last_poll_ms != 0 && (millis() - last_poll_ms < interval_ms)) {
        return;
    }

    last_poll_ms = millis();

    if (cfg.data_source == 1) {
        dexcom_fetch_glucose();
    } else {
        generic_fetch();
    }
}

const GlucoseReading& http_get_reading() {
    return current_reading;
}

int http_get_failure_count() {
    return failure_count;
}

int http_get_last_response_code() {
    return last_response_code;
}

const char* http_get_last_response_body() {
    return last_response_body;
}

bool http_has_ever_received() {
    return ever_received;
}

unsigned long http_time_since_last_reading() {
    if (!ever_received || last_success_ms == 0) {
        return ULONG_MAX;
    }
    return millis() - last_success_ms;
}

int http_get_delta() {
    return current_delta;
}

int http_get_history(GlucoseHistoryEntry* out, int max_count) {
    if (history_count == 0) return 0;

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
    return count;
}
