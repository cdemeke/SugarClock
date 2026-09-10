#include "libre_client.h"
#include "config_manager.h"
#include "time_engine.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>
#include <stdarg.h>
#include "mbedtls/md.h"

// LibreLinkUp rejects clients that report an outdated app version (status 920).
// If logins start failing with "requires app version", bump LLU_VERSION.
// Keep in sync with scripts/libre_probe.py.
#define LLU_VERSION       "4.16.0"
#define LLU_PRODUCT       "llu.android"
#define LLU_DEFAULT_HOST  "api.libreview.io"
#define LLU_STATUS_OK            0
#define LLU_STATUS_NEEDS_ACTION  4     // terms of use / privacy policy / verify email
#define LLU_SESSION_LIFETIME_MS  (24UL * 3600000UL)  // re-login daily
#define LLU_MAX_READING_AGE_SEC  (10 * 60)          // same 10-minute window as Dexcom
#define LLU_BACKOFF_MIN_MS       (5UL * 60000UL)
#define LLU_BACKOFF_MAX_MS       (60UL * 60000UL)

enum LoginResult { LOGIN_OK, LOGIN_TRANSIENT, LOGIN_REJECTED };

static char auth_token[1024] = "";
static char account_id[65] = "";   // sha256(user.id) as hex, sent as "account-id"
static unsigned long session_time_ms = 0;

// Rejected logins (bad password, unaccepted terms) back off so we don't hammer
// Abbott's auth endpoint every poll and risk an account lockout.
static unsigned long login_backoff_ms = 0;
static unsigned long login_failed_at_ms = 0;

static int last_http_code = 0;
static char last_message[160] = "";

static void set_message(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_message, sizeof(last_message), fmt, args);
    va_end(args);
    Serial.printf("[LIBRE] %s\n", last_message);
}

static void host_for_region(const char* region, char* out, size_t n) {
    if (!region || !region[0]) {
        snprintf(out, n, "%s", LLU_DEFAULT_HOST);
    } else if (strcmp(region, "ru") == 0) {
        snprintf(out, n, "api.libreview.ru");
    } else if (strcmp(region, "cn") == 0) {
        snprintf(out, n, "api-cn.myfreestyle.cn");
    } else {
        snprintf(out, n, "api-%s.libreview.io", region);
    }
}

static void sha256_hex(const char* in, char out[65]) {
    uint8_t hash[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
               (const uint8_t*)in, strlen(in), hash);
    for (int i = 0; i < 32; i++) {
        snprintf(out + i * 2, 3, "%02x", hash[i]);
    }
}

// LibreLinkUp TrendArrow: 1=SingleDown 2=FortyFiveDown 3=Flat 4=FortyFiveUp 5=SingleUp.
// Libre has no double arrows, so its steepest arrows map to our "fast" arrows.
static TrendType map_trend(int arrow) {
    switch (arrow) {
        case 1: return TREND_FALLING_FAST;
        case 2: return TREND_FALLING;
        case 3: return TREND_FLAT;
        case 4: return TREND_RISING;
        case 5: return TREND_RISING_FAST;
        default: return TREND_UNKNOWN;
    }
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm)
static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// Parse FactoryTimestamp ("9/10/2026 6:40:00 PM", always UTC) to epoch seconds
static unsigned long parse_timestamp(const char* s) {
    int mo, d, y, h, mi, se;
    char ampm[3] = "";
    if (sscanf(s, "%d/%d/%d %d:%d:%d %2s", &mo, &d, &y, &h, &mi, &se, ampm) < 6) {
        return 0;
    }
    if ((ampm[0] == 'P' || ampm[0] == 'p') && h < 12) h += 12;
    if ((ampm[0] == 'A' || ampm[0] == 'a') && h == 12) h = 0;
    return (unsigned long)(days_from_civil(y, mo, d) * 86400L + h * 3600L + mi * 60L + se);
}

// Perform a LibreLinkUp request against the configured region. body == nullptr → GET.
static int llu_request(const char* path, const char* body, String& response, bool authed) {
    AppConfig& cfg = config_get();
    char host[48];
    host_for_region(cfg.libre_region, host, sizeof(host));
    char url[128];
    snprintf(url, sizeof(url), "https://%s%s", host, path);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    HTTPClient http;
    if (!http.begin(client, url)) {
        last_http_code = -1;
        return -1;
    }

    http.setTimeout(15000);
    http.addHeader("product", LLU_PRODUCT);
    http.addHeader("version", LLU_VERSION);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("Cache-Control", "no-cache");
    if (authed) {
        char bearer[sizeof(auth_token) + 8];
        snprintf(bearer, sizeof(bearer), "Bearer %s", auth_token);
        http.addHeader("Authorization", bearer);
        http.addHeader("account-id", account_id);
    }

    int code = body ? http.POST(body) : http.GET();
    response = (code > 0) ? http.getString() : String();
    http.end();

    last_http_code = code;
    return code;
}

static LoginResult llu_login() {
    AppConfig& cfg = config_get();

    JsonDocument req;
    req["email"] = cfg.libre_email;
    req["password"] = cfg.libre_password;
    String body;
    serializeJson(req, body);

    JsonDocument filter;
    filter["status"] = true;
    filter["error"]["message"] = true;
    filter["data"]["redirect"] = true;
    filter["data"]["region"] = true;
    filter["data"]["step"]["type"] = true;
    filter["data"]["minimumVersion"] = true;
    filter["data"]["authTicket"]["token"] = true;
    filter["data"]["user"]["id"] = true;

    // Login at the saved region (or the global host), following one region redirect
    for (int attempt = 0; attempt < 2; attempt++) {
        Serial.printf("[LIBRE] Logging in (region: %s)...\n",
                      cfg.libre_region[0] ? cfg.libre_region : "auto");

        String resp;
        int code = llu_request("/llu/auth/login", body.c_str(), resp, false);
        if (code == 401 || code == 403 || code == 429) {
            set_message("Login rejected: HTTP %d", code);
            return LOGIN_REJECTED;
        }
        if (code != HTTP_CODE_OK) {
            set_message("Login failed: HTTP %d", code);
            return LOGIN_TRANSIENT;
        }

        JsonDocument doc;
        if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) {
            set_message("Login: unexpected response");
            return LOGIN_TRANSIENT;
        }

        int status = doc["status"] | -1;
        JsonObject data = doc["data"];

        if (data["redirect"] | false) {
            // Region codes are short lowercase identifiers ("us", "eu2", ...)
            const char* region = data["region"] | "";
            char clean[sizeof(cfg.libre_region)] = "";
            size_t n = 0;
            for (const char* p = region; *p && n < sizeof(clean) - 1; p++) {
                if (isalnum((unsigned char)*p)) clean[n++] = tolower((unsigned char)*p);
            }
            clean[n] = '\0';
            if (n == 0 || strcmp(clean, cfg.libre_region) == 0) {
                set_message("Login: bad region redirect '%s'", region);
                return LOGIN_TRANSIENT;
            }
            Serial.printf("[LIBRE] Redirected to region '%s'\n", clean);
            strncpy(cfg.libre_region, clean, sizeof(cfg.libre_region) - 1);
            config_save();
            continue;
        }

        if (status == LLU_STATUS_NEEDS_ACTION) {
            set_message("Open the LibreLinkUp app and accept the %s",
                        data["step"]["type"] | "terms");
            return LOGIN_REJECTED;
        }
        if (data["minimumVersion"].is<const char*>()) {
            set_message("LibreLinkUp requires app version %s - firmware update needed",
                        data["minimumVersion"].as<const char*>());
            return LOGIN_REJECTED;
        }
        if (status != LLU_STATUS_OK) {
            set_message("Login failed (status %d): %s", status,
                        doc["error"]["message"] | "check email and password");
            return LOGIN_REJECTED;
        }

        const char* token = data["authTicket"]["token"] | "";
        const char* user_id = data["user"]["id"] | "";
        if (!token[0] || !user_id[0] || strlen(token) >= sizeof(auth_token)) {
            set_message("Login: missing or oversized auth ticket");
            return LOGIN_TRANSIENT;
        }

        strncpy(auth_token, token, sizeof(auth_token) - 1);
        auth_token[sizeof(auth_token) - 1] = '\0';
        sha256_hex(user_id, account_id);
        session_time_ms = millis();
        Serial.println("[LIBRE] Login OK");
        return LOGIN_OK;
    }

    set_message("Login: too many region redirects");
    return LOGIN_TRANSIENT;
}

bool libre_fetch(LibreReading& out) {
    bool need_login = auth_token[0] == '\0' ||
                      millis() - session_time_ms > LLU_SESSION_LIFETIME_MS;

    if (need_login) {
        if (login_backoff_ms > 0 && millis() - login_failed_at_ms < login_backoff_ms) {
            unsigned long wait_s = (login_backoff_ms - (millis() - login_failed_at_ms)) / 1000;
            Serial.printf("[LIBRE] Login backoff, retry in %lus: %s\n", wait_s, last_message);
            return false;
        }

        auth_token[0] = '\0';
        LoginResult result = llu_login();
        if (result == LOGIN_REJECTED) {
            login_backoff_ms = login_backoff_ms == 0
                ? LLU_BACKOFF_MIN_MS
                : min(login_backoff_ms * 2, LLU_BACKOFF_MAX_MS);
            login_failed_at_ms = millis();
        }
        if (result != LOGIN_OK) return false;
        login_backoff_ms = 0;
    }

    String resp;
    int code = llu_request("/llu/connections", nullptr, resp, true);
    if (code == 401 || code == 403) {
        auth_token[0] = '\0';
        set_message("Session expired, will log in again");
        return false;
    }
    if (code != HTTP_CODE_OK) {
        set_message("Connections failed: HTTP %d", code);
        return false;
    }

    JsonDocument filter;
    filter["status"] = true;
    filter["data"][0]["glucoseMeasurement"]["ValueInMgPerDl"] = true;
    filter["data"][0]["glucoseMeasurement"]["TrendArrow"] = true;
    filter["data"][0]["glucoseMeasurement"]["FactoryTimestamp"] = true;

    JsonDocument doc;
    if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) {
        set_message("Connections: unexpected response");
        return false;
    }

    int status = doc["status"] | -1;
    if (status != LLU_STATUS_OK) {
        auth_token[0] = '\0';
        set_message("Connections failed (status %d), will log in again", status);
        return false;
    }

    JsonArray conns = doc["data"].as<JsonArray>();
    if (conns.size() == 0) {
        set_message("No one is sharing with this LibreLinkUp account");
        return false;
    }
    if (conns.size() > 1) {
        Serial.printf("[LIBRE] %d connections, using the first\n", (int)conns.size());
    }

    JsonObject gm = conns[0]["glucoseMeasurement"];
    int glucose = (int)lroundf(gm["ValueInMgPerDl"] | 0.0f);
    if (glucose <= 0) {
        set_message("No glucose value yet (sensor warming up?)");
        return false;
    }

    unsigned long ts = parse_timestamp(gm["FactoryTimestamp"] | "");
    if (ts > 0 && time_is_available()) {
        long age = (long)time(nullptr) - (long)ts;
        if (age > LLU_MAX_READING_AGE_SEC) {
            set_message("Latest Libre reading is %ld min old", age / 60);
            return false;
        }
    }

    out.glucose = glucose;
    out.trend = map_trend(gm["TrendArrow"] | 0);
    out.timestamp = ts;
    set_message("OK: %d mg/dL, arrow %d", glucose, gm["TrendArrow"] | 0);
    return true;
}

void libre_api_host(char* out, size_t n) {
    host_for_region(config_get().libre_region, out, n);
}

void libre_reset_session() {
    auth_token[0] = '\0';
    account_id[0] = '\0';
    session_time_ms = 0;
    login_backoff_ms = 0;
    login_failed_at_ms = 0;
}

int libre_last_http_code() {
    return last_http_code;
}

const char* libre_last_message() {
    return last_message;
}
