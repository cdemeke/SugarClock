#include "http_client.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>

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
static std::atomic<bool> http_paused{false};
static std::atomic<bool> fetch_running{false},fetch_complete{false},force_requested{false};
static portMUX_TYPE published_mux=portMUX_INITIALIZER_UNLOCKED;
struct PublishedHTTP {
 GlucoseReading reading;
 GlucoseHistoryEntry history[GLUCOSE_HISTORY_SIZE];
 int failure_count=0,code=0,delta=0,history_count=0,history_write=0;
 bool ever=false;
 unsigned long last_success=0;
};
static PublishedHTTP published;
static std::atomic<unsigned long> fetch_generation{0};
unsigned long http_fetch_generation() {return fetch_generation;}
bool http_is_fetching() {return fetch_running;}


// Delta tracking
static int prev_glucose = 0;
static int current_delta = 0;
static bool has_prev_reading = false;
static unsigned long last_recorded_timestamp = 0;

// History circular buffer
static GlucoseHistoryEntry history_buf[GLUCOSE_HISTORY_SIZE];
static int history_write_idx = 0;
static int history_count = 0;

// Dexcom session state
static char dexcom_session_id[64] = "";
static unsigned long dexcom_session_time_ms = 0;

// Demo mode: synthetic glucose that gently wanders in a normal 80-100 range.
// Used for recording videos / demos without a real CGM connection.
#define DEMO_UPDATE_MS 5000
static unsigned long demo_last_update_ms = 0;
static int demo_value = 90;

// Record a glucose value to history and update delta.
// reading_timestamp is the CGM timestamp (epoch seconds) so we can skip
// duplicate readings that arrive when we poll faster than the CGM updates.
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
    AppConfig cfg = config_snapshot();
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
    strncpy(last_response_body, "Response body omitted from diagnostics", sizeof(last_response_body) - 1);
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
    strncpy(last_response_body, "Response body omitted from diagnostics", sizeof(last_response_body) - 1);
    last_response_code = loginCode;

    Serial.printf("[DEXCOM] Auth step 2: HTTP %d\n", loginCode);

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
        Serial.println("[DEXCOM] Login OK");
        return true;
    }

    Serial.printf("[DEXCOM] Login failed: HTTP %d\n", loginCode);
    return false;
}

// Dexcom Share: fetch latest glucose reading
static bool dexcom_fetch_glucose() {
    AppConfig cfg = config_snapshot();

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
        strncpy(last_response_body, "Response body omitted from diagnostics", sizeof(last_response_body) - 1);
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
                current_reading.timestamp = (unsigned long)(strtoull(start + 1, NULL, 10) / 1000ULL);
            }
        }

        current_reading.valid = (current_reading.glucose > 0);

        if (current_reading.valid) {
            record_reading(current_reading.glucose, current_reading.timestamp);
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
    strncpy(last_response_body, "Response body omitted from diagnostics", sizeof(last_response_body) - 1);
    Serial.printf("[DEXCOM] Fetch failed: HTTP %d\n", httpCode);
    failure_count++;
    http.end();
    return false;
}

// Generic URL fetch (original behavior)
static void generic_fetch() {
    AppConfig cfg = config_snapshot();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    Serial.println("[HTTP] Polling configured endpoint");

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
        strncpy(last_response_body, "Response body omitted from diagnostics", sizeof(last_response_body) - 1);
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
                record_reading(current_reading.glucose, current_reading.timestamp);
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

// Demo mode: generate a synthetic in-range reading. Wanders gently via a
// small random walk clamped to 80-100 so the display looks "alive" on camera.
static void demo_generate() {
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
    // overwrites prev_glucose).
    int delta = has_prev_reading ? (demo_value - prev_glucose) : 0;
    if (delta > 1)       current_reading.trend = TREND_RISING;
    else if (delta < -1) current_reading.trend = TREND_FALLING;
    else                 current_reading.trend = TREND_FLAT;

    current_reading.glucose = demo_value;
    current_reading.received_at_ms = now;
    current_reading.force_mode = -1;
    current_reading.message[0] = '\0';
    current_reading.timestamp = now / 1000;   // synthetic, unique per update
    current_reading.valid = true;

    record_reading(demo_value, current_reading.timestamp);
    failure_count = 0;
    ever_received = true;
    last_response_code = 200;
    last_success_ms = now;
    strncpy(last_response_body, "demo mode", sizeof(last_response_body) - 1);
}

static void publish_result() {
 portENTER_CRITICAL(&published_mux);
 published.reading=current_reading;memcpy(published.history,history_buf,sizeof(history_buf));
 published.failure_count=failure_count;published.code=last_response_code;published.delta=current_delta;
 published.history_count=history_count;published.history_write=history_write_idx;
 published.ever=ever_received;published.last_success=last_success_ms;
 portEXIT_CRITICAL(&published_mux);
}
static void fetch_worker(void* parameter) {
 int source=int(reinterpret_cast<intptr_t>(parameter));
 Serial.printf("[GLUCOSE MEM] start free=%u min=%u largest=%u\n",ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 if(source==1) dexcom_fetch_glucose();else generic_fetch();
 Serial.printf("[GLUCOSE MEM] end free=%u min=%u largest=%u\n",ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 fetch_complete=true;fetch_running=false;
 vTaskDelete(nullptr);
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
    last_recorded_timestamp = 0;

    // Demo mode state
    demo_last_update_ms = 0;
    demo_value = 90;
    ever_received=false;failure_count=0;last_response_code=0;
    publish_result();
}

static std::atomic<bool> configuration_changed{false};
void http_configuration_changed() {configuration_changed=true;}
void http_loop() {
    if(fetch_running) return;
    if(fetch_complete.exchange(false)) {publish_result();++fetch_generation;}
    if(configuration_changed.exchange(false)) http_init();
    if(http_paused) return;
    AppConfig cfg=config_snapshot();
    bool force=force_requested.exchange(false);
    if(cfg.data_source==2) {if(force)demo_last_update_ms=0;unsigned long before=demo_last_update_ms;demo_generate();if(before!=demo_last_update_ms) {publish_result();++fetch_generation;}return;}
    if(!wifi_is_connected() || !config_has_server()) return;
    unsigned long interval_ms=max(15,cfg.poll_interval_sec)*1000UL;
    if(!force && last_poll_ms && millis()-last_poll_ms<interval_ms) return;
    last_poll_ms=millis();fetch_running=true;
    if(xTaskCreate(fetch_worker,"glucose_https",14336,reinterpret_cast<void*>(static_cast<intptr_t>(cfg.data_source)),1,nullptr)!=pdPASS) {
        fetch_running=false;last_response_code=-1000;++failure_count;publish_result();
    }
}

GlucoseReading http_get_reading() {
 portENTER_CRITICAL(&published_mux);auto reading=published.reading;portEXIT_CRITICAL(&published_mux);return reading;
}
int http_get_failure_count() {portENTER_CRITICAL(&published_mux);int n=published.failure_count;portEXIT_CRITICAL(&published_mux);return n;}
int http_get_last_response_code() {portENTER_CRITICAL(&published_mux);int n=published.code;portEXIT_CRITICAL(&published_mux);return n;}
const char* http_get_last_response_body() {return "Response bodies omitted from diagnostics";}
bool http_has_ever_received() {portENTER_CRITICAL(&published_mux);bool b=published.ever;portEXIT_CRITICAL(&published_mux);return b;}
unsigned long http_time_since_last_reading() {
 portENTER_CRITICAL(&published_mux);bool ever=published.ever;unsigned long last=published.last_success;portEXIT_CRITICAL(&published_mux);
 return !ever || !last ? ULONG_MAX:millis()-last;
}
int http_get_delta() {portENTER_CRITICAL(&published_mux);int n=published.delta;portEXIT_CRITICAL(&published_mux);return n;}
bool http_force_fetch() {
 if(http_paused) return false;
 force_requested=true;return true;
}
int http_get_history(GlucoseHistoryEntry* out,int max_count) {
 if(!out || max_count<=0) return 0;
 portENTER_CRITICAL(&published_mux);
 int count=min(max_count,published.history_count);
 int start=published.history_count<GLUCOSE_HISTORY_SIZE?0:published.history_write;
 for(int i=0;i<count;++i) out[i]=published.history[(start+published.history_count-count+i)%GLUCOSE_HISTORY_SIZE];
 portEXIT_CRITICAL(&published_mux);return count;
}

void http_set_paused(bool paused) {
    http_paused = paused;
}

bool http_is_paused() {
    return http_paused;
}
