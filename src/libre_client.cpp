#include "libre_client.h"
#include "libre_session.h"
#include "libre_trusted_roots.h"
#include "config_manager.h"
#include "time_engine.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>
#include <stdarg.h>
#include <atomic>
#include <memory>
#include "mbedtls/md.h"

// LibreLinkUp rejects clients that report an outdated app version (status 920).
// If logins start failing with "requires app version", bump LLU_VERSION.
// Keep in sync with scripts/libre_probe.py.
#define LLU_VERSION       "4.16.0"
#define LLU_PRODUCT       "llu.android"
#define LLU_DEFAULT_HOST  "api.libreview.io"
#define LLU_STATUS_OK            0
#define LLU_STATUS_NEEDS_ACTION  4     // terms of use / privacy policy / verify email

static char auth_token[1024] = "";
static char account_id[65] = "";   // sha256(user.id) as hex, sent as "account-id"

static LibreSession session;
static std::atomic<bool> fetch_active{false};
static std::atomic<bool> reset_pending{false};
static LibrePatientCache patient_cache;

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

// Perform a LibreLinkUp request against the configured region. body == nullptr → GET.
// On a connection failure, tls_error receives mbedtls' reason (e.g. a rejected certificate).
static int llu_request(const char* path, const char* body, String& response, bool authed,
                       char* tls_error, size_t tls_error_size, const char* region) {
    char host[48];
    host_for_region(region, host, sizeof(host));
    char url[128];
    snprintf(url, sizeof(url), "https://%s%s", host, path);
    tls_error[0] = '\0';

    // Credentials and readings only go to a server whose certificate chains to
    // a pinned root and matches the hostname; never setInsecure() here.
    WiFiClientSecure client;
    client.setCACert(LIBRE_TRUSTED_ROOTS_PEM);
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
    if (code > 0) {
        response = http.getString();
    } else {
        response = String();
        client.lastError(tls_error, tls_error_size);
    }
    http.end();

    last_http_code = code;
    return code;
}

static void set_connection_error(const char* what, int code, const char* tls_error) {
    if (code < 0 && tls_error[0]) {
        set_message("%s: secure connection failed (%s)", what, tls_error);
    } else {
        set_message("%s: HTTP %d", what, code);
    }
}

// HTTP implementation of the LibreLinkUp transport used by LibreSession
class HttpLibreTransport : public LibreTransport {
    LibreConfigSnapshot original_;
    char region_[8] = "";
    LibrePatient patient_ = {};
    bool config_dirty_ = false;
public:
    HttpLibreTransport() : original_(config_get()) {
        strcpy(region_, original_.region);
        strcpy(patient_.id, original_.patient_id);
        strcpy(patient_.name, original_.patient_name);
    }
    bool save_config_if_changed() {
        // Settings edits and unrelated NVS saves use the same lock. Validate
        // and commit together: a late response must never pair an old person
        // with new credentials, even before reset_pending has been set.
        LibreConfigLock lock(config_libre_mutex());
        AppConfig& cfg = config_get();
        if (reset_pending.load() || !original_.matches(cfg)) return false;
        if (config_dirty_ && config_apply_libre_discovery(
                cfg, original_, region_, patient_.id, patient_.name)) config_save();
        return true;
    }
    bool saw_empty = false;  // nobody shares with the account (message already set)

    LibreAuthResult login() override {
        JsonDocument req;
        req["email"] = original_.email;
        req["password"] = original_.password;
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

        auth_token[0] = '\0';

        // Login at the saved region (or the global host), following one region redirect
        for (int attempt = 0; attempt < 2; attempt++) {
            Serial.printf("[LIBRE] Logging in (region: %s)...\n",
                          region_[0] ? region_ : "auto");

            String resp;
            char tls_error[96];
            int code = llu_request("/llu/auth/login", body.c_str(), resp, false,
                                   tls_error, sizeof(tls_error), region_);
            if (reset_pending.load()) {
                set_message("Libre settings changed; retry with the saved account");
                return LIBRE_AUTH_TRANSIENT;
            }
            if (code == 401 || code == 403 || code == 429) {
                set_message("Login rejected: HTTP %d", code);
                return LIBRE_AUTH_REJECTED;
            }
            if (code != HTTP_CODE_OK) {
                set_connection_error("Login failed", code, tls_error);
                return LIBRE_AUTH_TRANSIENT;
            }

            JsonDocument doc;
            if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) {
                set_message("Login: unexpected response");
                return LIBRE_AUTH_TRANSIENT;
            }

            int status = doc["status"] | -1;
            JsonObject data = doc["data"];

            if (data["redirect"] | false) {
                // Region codes are short lowercase identifiers ("us", "eu2", ...)
                const char* region = data["region"] | "";
                char clean[sizeof(region_)] = "";
                size_t n = 0;
                for (const char* p = region; *p && n < sizeof(clean) - 1; p++) {
                    if (isalnum((unsigned char)*p)) clean[n++] = tolower((unsigned char)*p);
                }
                clean[n] = '\0';
                if (n == 0 || strcmp(clean, region_) == 0) {
                    set_message("Login: bad region redirect '%s'", region);
                    return LIBRE_AUTH_TRANSIENT;
                }
                Serial.printf("[LIBRE] Redirected to region '%s'\n", clean);
                // Keep redirects local until a login at that host succeeds.
                strcpy(region_, clean);
                continue;
            }

            if (status == LLU_STATUS_NEEDS_ACTION) {
                set_message("Open the LibreLinkUp app and accept the %s",
                            data["step"]["type"] | "terms");
                return LIBRE_AUTH_NEEDS_ACTION;
            }
            if (data["minimumVersion"].is<const char*>()) {
                set_message("LibreLinkUp requires app version %s - firmware update needed",
                            data["minimumVersion"].as<const char*>());
                return LIBRE_AUTH_REJECTED;
            }
            if (status != LLU_STATUS_OK) {
                set_message("Login failed (status %d): %s", status,
                            doc["error"]["message"] | "check email and password");
                return LIBRE_AUTH_REJECTED;
            }

            const char* token = data["authTicket"]["token"] | "";
            const char* user_id = data["user"]["id"] | "";
            if (!token[0] || !user_id[0] || strlen(token) >= sizeof(auth_token)) {
                set_message("Login: missing or oversized auth ticket");
                return LIBRE_AUTH_TRANSIENT;
            }

            strncpy(auth_token, token, sizeof(auth_token) - 1);
            auth_token[sizeof(auth_token) - 1] = '\0';
            sha256_hex(user_id, account_id);
            config_dirty_ = true;
            Serial.println("[LIBRE] Login OK");
            return LIBRE_AUTH_OK;
        }

        set_message("Login: too many region redirects");
        return LIBRE_AUTH_TRANSIENT;
    }

    LibreReadResult read_latest(LibreRawReading& out) override {
        String resp;
        char tls_error[96];
        int code = llu_request("/llu/connections", nullptr, resp, true,
                               tls_error, sizeof(tls_error), region_);
        if (reset_pending.load()) {
            set_message("Libre settings changed; retry with the saved account");
            return LIBRE_READ_TRANSIENT;
        }
        if (code == 401 || code == 403) {
            set_message("Connections refused: HTTP %d", code);
            return LIBRE_READ_UNAUTHORIZED;
        }
        if (code == 429) {
            set_message("Connections rate limited: HTTP 429");
            return LIBRE_READ_REJECTED;
        }
        if (code != HTTP_CODE_OK) {
            set_connection_error("Connections failed", code, tls_error);
            return LIBRE_READ_TRANSIENT;
        }

        JsonDocument filter;
        filter["status"] = true;
        filter["data"][0]["patientId"] = true;
        filter["data"][0]["firstName"] = true;
        filter["data"][0]["lastName"] = true;
        filter["data"][0]["glucoseMeasurement"]["ValueInMgPerDl"] = true;
        filter["data"][0]["glucoseMeasurement"]["TrendArrow"] = true;
        filter["data"][0]["glucoseMeasurement"]["FactoryTimestamp"] = true;

        JsonDocument doc;
        if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) {
            set_message("Connections: unexpected response");
            return LIBRE_READ_TRANSIENT;
        }

        int status = doc["status"] | -1;
        if (status != LLU_STATUS_OK) {
            set_message("Connections refused (status %d)", status);
            return LIBRE_READ_UNAUTHORIZED;
        }

        JsonArray conns = doc["data"].as<JsonArray>();
        if (conns.size() > LIBRE_MAX_PATIENTS) {
            patient_cache.clear();
            set_message("Too many Libre connections; use a follower account with fewer people");
            saw_empty = true;
            return LIBRE_READ_EMPTY;
        }
        auto snapshot = std::make_shared<LibrePatientSnapshot>(original_.email, conns.size());
        auto& available = snapshot->people;
        size_t count = 0;
        for (JsonObject conn : conns) {
            const char* id = conn["patientId"] | "";
            if (strlen(id) >= sizeof(available[count].id)) {
                patient_cache.clear();
                set_message("Libre returned an invalid person ID");
                saw_empty = true;
                return LIBRE_READ_EMPTY;
            }
            strcpy(available[count].id, id);
            snprintf(available[count].name, sizeof(available[count].name), "%s %s",
                     conn["firstName"] | "", conn["lastName"] | "");
            count++;
        }
        if (conns.size() == 0) {
            patient_cache.clear();
            set_message("No one is sharing with this LibreLinkUp account");
            saw_empty = true;
            return LIBRE_READ_EMPTY;
        }
        int selected = libre_patient_index(available.data(), count, original_.patient_id);
        if (selected == LIBRE_PATIENT_INVALID) patient_cache.clear();
        else patient_cache.publish(snapshot);
        if (selected < 0) {
            if (selected == LIBRE_PATIENT_AMBIGUOUS) {
                set_message("Multiple people share with this account; select a person in Settings and save");
            } else if (selected == LIBRE_PATIENT_MISSING) {
                set_message("The saved person is no longer sharing; check LibreLinkUp or select a person in Settings");
            } else {
                set_message("Libre returned missing or duplicate person IDs");
            }
            saw_empty = true;
            return LIBRE_READ_EMPTY;
        }
        patient_ = available[selected];
        config_dirty_ = true;

        JsonObject gm = conns[selected]["glucoseMeasurement"];
        out.glucose = (int)lroundf(gm["ValueInMgPerDl"] | 0.0f);
        out.trend_arrow = gm["TrendArrow"] | 0;
        const char* timestamp = gm["FactoryTimestamp"] | "";
        // Do not turn an unrecognized long value into a valid-looking prefix.
        if (strlen(timestamp) >= sizeof(out.factory_timestamp)) out.factory_timestamp[0] = '\0';
        else strcpy(out.factory_timestamp, timestamp);
        return LIBRE_READ_OK;
    }
};

bool libre_fetch(LibreReading& out, bool* attempted) {
    if (attempted) *attempted = false;
    // Web tests and the main loop may run on different tasks. Only one may
    // consume the retry gate or use the transport at a time.
    if (fetch_active.exchange(true)) {
        last_http_code = 429;
        set_message("A Libre request is already in progress; try again shortly");
        return false;
    }
    struct Release {
        ~Release() { fetch_active.store(false); }
    } release;
    if (reset_pending.exchange(false)) {
        session.reset();
        auth_token[0] = '\0';
        account_id[0] = '\0';
    }
    HttpLibreTransport transport;
    bool synced = time_is_network_synced();
    LibreResult r = session.fetch(transport, millis(), synced, synced ? (uint32_t)time(nullptr) : 0);
    if (attempted) *attempted = r.status != LIBRE_FETCH_BACKOFF && r.status != LIBRE_FETCH_CLOCK_UNSYNCED;
    if (reset_pending.load()) return false; // discard an in-flight old-account reading
    // Coalesce a successful region redirect and first person binding into one
    // NVS write. Failed logins never persist their speculative redirect.
    if (!transport.save_config_if_changed()) return false;

    switch (r.status) {
        case LIBRE_FETCH_OK:
            out.glucose = r.glucose;
            out.trend = r.trend;
            out.timestamp = r.timestamp;
            out.age_sec = r.age_sec;
            set_message("OK: %d mg/dL, arrow %s, %lus old", r.glucose, TREND_NAMES[r.trend],
                        (unsigned long)r.age_sec);
            return true;
        case LIBRE_FETCH_CLOCK_UNSYNCED:
            last_http_code = 0;
            set_message("Waiting for network time before reading Libre data");
            return false;
        case LIBRE_FETCH_BACKOFF:
            last_http_code = 429;
            set_message(r.account_action_required
                            ? "Complete the required action in LibreLinkUp; retry in %lu seconds"
                            : "Libre retry paused; try again in %lu seconds",
                        (unsigned long)((r.retry_in_ms + 999) / 1000));
            return false;
        case LIBRE_FETCH_NEEDS_ACTION:
            return false; // Keep the specific action reported by the login.
        case LIBRE_FETCH_REJECTED:
            Serial.printf("[LIBRE] Rejected, retrying in %lu min\n",
                          (unsigned long)(r.retry_in_ms / 60000));
            return false;
        case LIBRE_FETCH_STALE:
            set_message("Latest Libre reading is %lu min old", (unsigned long)(r.age_sec / 60));
            return false;
        case LIBRE_FETCH_BAD_TIMESTAMP:
            set_message("Libre reading has an invalid timestamp");
            return false;
        case LIBRE_FETCH_NO_DATA:
            if (!transport.saw_empty) set_message("No glucose value yet (sensor warming up?)");
            return false;
        case LIBRE_FETCH_TRANSIENT:
            return false;  // transport already recorded the reason
    }
    return false;
}

void libre_api_host(char* out, size_t n) {
    LibreConfigLock lock(config_libre_mutex());
    host_for_region(config_get().libre_region, out, n);
}

void libre_reset_session() {
    reset_pending.store(true);
}

LibrePatients libre_get_patients() {
    LibreConfigLock lock(config_libre_mutex());
    return patient_cache.get(config_get().libre_email);
}

int libre_last_http_code() {
    return last_http_code;
}

const char* libre_last_message() {
    return last_message;
}
