#include "wifi_manager.h"
#include "wifi_trial_policy.h"
#include "config_manager.h"
#include "captive_portal.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <Arduino.h>

// The 802.1X ("enterprise") client API was renamed between ESP-IDF releases.
// platformio.ini pins no framework version, so support whichever header the
// resolved arduino-esp32 core ships.
#if __has_include(<esp_eap_client.h>)
  #include <esp_eap_client.h>          // IDF 5.x  / arduino-esp32 3.x
  #define EAP_SET_IDENTITY(b, n)       esp_eap_client_set_identity((b), (n))
  #define EAP_SET_USERNAME(b, n)       esp_eap_client_set_username((b), (n))
  #define EAP_SET_PASSWORD(b, n)       esp_eap_client_set_password((b), (n))
  #define EAP_SET_CA_CERT(b, n)        esp_eap_client_set_ca_cert((b), (n))
  #define EAP_CLEAR_CA_CERT()          esp_eap_client_clear_ca_cert()
  #define EAP_SET_TTLS_PHASE2(m)       esp_eap_client_set_ttls_phase2_method(m)
  #define EAP_SET_DISABLE_TIME_CHECK(v) esp_eap_client_set_disable_time_check(v)
  #define EAP_ENABLE()                 esp_wifi_sta_enterprise_enable()
  #define EAP_DISABLE()                esp_wifi_sta_enterprise_disable()
#else
  #include <esp_wpa2.h>                // IDF 4.x  / arduino-esp32 2.x
  #define EAP_SET_IDENTITY(b, n)       esp_wifi_sta_wpa2_ent_set_identity((b), (n))
  #define EAP_SET_USERNAME(b, n)       esp_wifi_sta_wpa2_ent_set_username((b), (n))
  #define EAP_SET_PASSWORD(b, n)       esp_wifi_sta_wpa2_ent_set_password((b), (n))
  #define EAP_SET_CA_CERT(b, n)        esp_wifi_sta_wpa2_ent_set_ca_cert((b), (n))
  #define EAP_CLEAR_CA_CERT()          esp_wifi_sta_wpa2_ent_clear_ca_cert()
  #define EAP_SET_TTLS_PHASE2(m)       esp_wifi_sta_wpa2_ent_set_ttls_phase2_method(m)
  #define EAP_SET_DISABLE_TIME_CHECK(v) esp_wifi_sta_wpa2_ent_set_disable_time_check(v)
  #define EAP_ENABLE()                 esp_wifi_sta_wpa2_ent_enable()
  #define EAP_DISABLE()                esp_wifi_sta_wpa2_ent_disable()
#endif

// Enterprise association carries a full RADIUS round trip on top of the normal
// 4-way handshake and is materially slower than PSK; 15s produced false failures.
#define WIFI_CONNECT_TIMEOUT_MS   25000
#define WIFI_RETRY_INTERVAL_MS    30000
// How long the STA may keep failing before the setup portal comes up. The STA
// keeps retrying underneath — the portal is additive, never a replacement.
#define WIFI_SETUP_AP_AFTER_MS    120000
// Grace period between a successful trial and tearing the AP down, so the
// phone still on the AP has a chance to poll /api/wifi/status once more.
#define WIFI_AP_SHUTDOWN_GRACE_MS 10000

#define SETUP_AP_SSID "SugarClock-Setup"

#define MAX_CA_PEM 4096

static char ip_buf[16] = "0.0.0.0";
static char ap_ip_buf[16] = "0.0.0.0";
static const char* status_str = "IDLE";

static WifiState state = WIFI_ST_IDLE;
static unsigned long attempt_start_ms = 0;
static unsigned long retry_wait_start_ms = 0;
static unsigned long first_failure_ms = 0;
static bool portal_up = false;
static unsigned long portal_shutdown_at_ms = 0;
static bool boot_connect_pending = false;

// Written from the WiFi event task, read from loop()
static volatile int last_disconnect_reason = 0;
static volatile uint32_t disconnect_count = 0;
static volatile bool assoc_done = false;

// Trial state
static WifiTrialParams trial_params;
static WifiTrialParams pending_params;
static volatile bool trial_requested = false;
static bool trial_active = false;
static unsigned long trial_start_ms = 0;
static WifiTrialState trial_state = WIFI_TRIAL_IDLE;
static char trial_detail[96] = "";

// Scan cache
static WifiScanEntry scan_cache[WIFI_SCAN_MAX];
static int scan_cache_count = 0;
static bool scan_running = false;
static unsigned long scan_completed_ms = 0;

// ---------------------------------------------------------------------------
// Helpers

static void set_ip_from_sta() {
    IPAddress ip = WiFi.localIP();
    snprintf(ip_buf, sizeof(ip_buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

static bool auth_mode_is_enterprise(uint8_t mode) {
    if (mode == WIFI_AUTH_WPA2_ENTERPRISE) return true;
#ifdef WIFI_AUTH_WPA3_ENT_192
    if (mode == WIFI_AUTH_WPA3_ENT_192) return true;
#endif
#ifdef WIFI_AUTH_WPA3_ENTERPRISE
    if (mode == WIFI_AUTH_WPA3_ENTERPRISE) return true;
#endif
#ifdef WIFI_AUTH_ENTERPRISE
    if (mode == WIFI_AUTH_ENTERPRISE) return true;
#endif
    return false;
}

// Map an esp-idf disconnect reason onto something a user can act on. The point
// of the split is that "no such network" and "wrong password" have completely
// different fixes, and a single "failed" tells the user neither.
static WifiTrialState classify_reason(int reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
            return WIFI_TRIAL_FAILED_NO_AP;
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_802_1X_AUTH_FAILED:
        case WIFI_REASON_IE_IN_4WAY_DIFFERS:
            return WIFI_TRIAL_FAILED_AUTH;
        default:
            return WIFI_TRIAL_FAILED_TIMEOUT;
    }
}

static void on_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            assoc_done = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            last_disconnect_reason = info.wifi_sta_disconnected.reason;
            disconnect_count++;
            assoc_done = false;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            break;
        default:
            break;
    }
}

// Configure the 802.1X supplicant for the next WiFi.begin(). Must run after the
// WiFi driver is up (WiFi.mode) and before begin().
static void enterprise_apply(const WifiTrialParams& p) {
    // Outer identity. Blank means "use the real username", which is what a phone
    // does by default.
    const char* anon = (p.anon_identity[0] != '\0') ? p.anon_identity : p.identity;
    EAP_SET_IDENTITY((const uint8_t*)anon, strlen(anon));
    EAP_SET_USERNAME((const uint8_t*)p.identity, strlen(p.identity));
    EAP_SET_PASSWORD((const uint8_t*)p.password, strlen(p.password));

    // PEAP needs no phase-2 call (the supplicant negotiates MSCHAPv2); TTLS does.
    if (p.eap_method == 1) {
        EAP_SET_TTLS_PHASE2(ESP_EAP_TTLS_PHASE2_MSCHAPV2);
    }

    static char ca_pem[MAX_CA_PEM];
    bool ca_loaded = false;
    if (p.validate_ca && config_ca_exists()) {
        size_t stored = config_ca_size();
        size_t n = (stored > 0 && stored < sizeof(ca_pem))
            ? config_ca_read(ca_pem, sizeof(ca_pem)) : 0;
        if (n == stored && n > 0) {
            // +1: the supplicant expects the terminating NUL to be counted
            EAP_SET_CA_CERT((const uint8_t*)ca_pem, n + 1);
            ca_loaded = true;
        } else {
            Serial.println("[WIFI] CA certificate is empty or too large; validation disabled for this attempt");
        }
    }
    if (!ca_loaded) {
        // Default: no server-certificate validation. This is the same trust
        // decision a phone makes when the user taps "Trust" on the RADIUS cert.
        EAP_CLEAR_CA_CERT();
    }
    // The device has no valid clock before NTP, so certificate validity dates
    // cannot be checked meaningfully at association time.
    EAP_SET_DISABLE_TIME_CHECK(true);

    EAP_ENABLE();
    Serial.printf("[WIFI] Enterprise: method=%s identity='%s' ca=%s\n",
                  p.eap_method == 1 ? "TTLS" : "PEAP", p.identity,
                  ca_loaded ? "validated" : "trusted");
}

// Start one association attempt with the given credentials.
static void start_attempt(const WifiTrialParams& p) {
    disconnect_count = 0;
    last_disconnect_reason = 0;
    assoc_done = false;

    WiFi.disconnect(false);

    if (p.security == 1) {
        enterprise_apply(p);
        WiFi.begin(p.ssid);
    } else {
        // Leaving stale EAP state enabled would break a later personal network
        EAP_DISABLE();
        WiFi.begin(p.ssid, p.password[0] ? p.password : NULL);
    }

    attempt_start_ms = millis();
    state = WIFI_ST_CONNECTING;
    status_str = "CONNECTING";
}

// Build trial params from the saved config
static void params_from_config(WifiTrialParams& p) {
    AppConfig& cfg = config_get();
    memset(&p, 0, sizeof(p));
    strncpy(p.ssid, cfg.wifi_ssid, sizeof(p.ssid) - 1);
    p.security = cfg.wifi_security;
    p.eap_method = cfg.wifi_eap_method;
    strncpy(p.identity, cfg.wifi_identity, sizeof(p.identity) - 1);
    strncpy(p.password,
            cfg.wifi_security == 1 ? cfg.wifi_eap_password : cfg.wifi_password,
            sizeof(p.password) - 1);
    strncpy(p.anon_identity, cfg.wifi_anon_identity, sizeof(p.anon_identity) - 1);
    p.validate_ca = cfg.wifi_validate_ca;
}

static bool persist_trial(const WifiTrialParams& p) {
    ConfigGuard guard;
    AppConfig& cfg = config_get();
    strncpy(cfg.wifi_ssid, p.ssid, sizeof(cfg.wifi_ssid) - 1);
    cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
    cfg.wifi_security = p.security;
    cfg.wifi_eap_method = p.eap_method;
    cfg.wifi_validate_ca = p.validate_ca;
    if (p.security == 1) {
        strncpy(cfg.wifi_identity, p.identity, sizeof(cfg.wifi_identity) - 1);
        strncpy(cfg.wifi_eap_password, p.password, sizeof(cfg.wifi_eap_password) - 1);
        strncpy(cfg.wifi_anon_identity, p.anon_identity, sizeof(cfg.wifi_anon_identity) - 1);
        cfg.wifi_password[0] = '\0';
    } else {
        strncpy(cfg.wifi_password, p.password, sizeof(cfg.wifi_password) - 1);
        cfg.wifi_identity[0] = '\0';
        cfg.wifi_eap_password[0] = '\0';
        cfg.wifi_anon_identity[0] = '\0';
    }
    return config_save();
}

// ---------------------------------------------------------------------------
// Setup portal

static void portal_start() {
    if (portal_up) return;

    // AP and STA must coexist: the STA keeps retrying the saved network and
    // scanning needs the STA interface up regardless.
    WiFi.mode(WIFI_AP_STA);
    // Deliberately open — the whole point is a frictionless on-site setup with
    // nothing to type before you can reach the page.
    WiFi.softAP(SETUP_AP_SSID);

    IPAddress apIp = WiFi.softAPIP();
    snprintf(ap_ip_buf, sizeof(ap_ip_buf), "%d.%d.%d.%d", apIp[0], apIp[1], apIp[2], apIp[3]);
    captive_portal_start(apIp);
    portal_up = true;
    portal_shutdown_at_ms = 0;
    Serial.printf("[WIFI] Setup AP up: SSID=%s  IP=%s\n", SETUP_AP_SSID, ap_ip_buf);
}

static void portal_stop() {
    if (!portal_up) return;
    captive_portal_stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    portal_up = false;
    portal_shutdown_at_ms = 0;
    strncpy(ap_ip_buf, "0.0.0.0", sizeof(ap_ip_buf));
    Serial.println("[WIFI] Setup AP shut down");
}

// ---------------------------------------------------------------------------
// Init

void wifi_init() {
    WiFi.onEvent(on_wifi_event);
    WiFi.persistent(false);
    // The state machine owns retry timing. Arduino auto-reconnect would otherwise
    // start associations behind our back while a phone is using the setup AP.
    WiFi.setAutoReconnect(false);

    if (!config_has_wifi()) {
        Serial.println("[WIFI] No WiFi credentials — starting setup portal");
        state = WIFI_ST_SETUP_AP;
        status_str = "SETUP AP";
        first_failure_ms = millis();
        portal_start();
        wifi_scan_start();
        return;
    }

    WiFi.mode(WIFI_STA);
    first_failure_ms = millis();
    state = WIFI_ST_IDLE;
    status_str = "SCANNING";
    boot_connect_pending = true;

    // One scan at boot seeds the cache used if the setup AP is needed later.
    // Association begins as soon as the asynchronous scan completes.
    if (!wifi_scan_start()) {
        WifiTrialParams p;
        params_from_config(p);
        boot_connect_pending = false;
        start_attempt(p);
    }
}

// ---------------------------------------------------------------------------
// Trial handling

bool wifi_trial_start(const WifiTrialParams& params) {
    ConfigGuard guard;
    if (params.ssid[0] == '\0') return false;
    if (trial_requested || trial_active) return false;
    pending_params = params;
    trial_requested = true;
    return true;
}

static void trial_begin() {
    // An explicit join takes precedence over a refresh scan. WiFi.begin() would
    // cancel the driver scan anyway, so keep our bookkeeping in sync.
    if (scan_running) {
        WiFi.scanDelete();
        scan_running = false;
    }
    { ConfigGuard guard;
    trial_params = pending_params;
    memset(&pending_params,0,sizeof(pending_params));
    trial_active = true;
    trial_requested = false; }
    trial_start_ms = millis();
    trial_state = WIFI_TRIAL_ASSOCIATING;
    trial_detail[0] = '\0';
    Serial.printf("[WIFI] Trial connect to '%s' (%s)\n", trial_params.ssid,
                  trial_params.security == 1 ? "enterprise" : "personal/open");
    start_attempt(trial_params);
}

static void trial_finish_failure(WifiTrialState st, int reason) {
    trial_state = st;
    trial_active = false;
    switch (st) {
        case WIFI_TRIAL_FAILED_NO_AP:
            snprintf(trial_detail, sizeof(trial_detail),
                     "Network '%s' was not found. Check the name, and that it is 2.4 GHz.",
                     trial_params.ssid);
            break;
        case WIFI_TRIAL_FAILED_AUTH:
            snprintf(trial_detail, sizeof(trial_detail),
                     "The network rejected the credentials (reason %d). Check the username format and password.",
                     reason);
            break;
        default:
            snprintf(trial_detail, sizeof(trial_detail),
                     "Timed out after %d seconds (last reason %d).",
                     WIFI_CONNECT_TIMEOUT_MS / 1000, reason);
            break;
    }
    Serial.printf("[WIFI] Trial failed: %s\n", trial_detail);

    // Fall back to whatever was saved so the device is not left idle
    WifiTrialParams p;
    params_from_config(p);
    switch(wifi_trial_recovery(p.ssid[0]!=0,portal_up)) {
        case WifiTrialRecovery::RetrySavedInPortal:
            WiFi.disconnect(false);state=WIFI_ST_RETRY_WAIT;
            retry_wait_start_ms=millis();status_str="SETUP AP";break;
        case WifiTrialRecovery::ReconnectSaved:
            start_attempt(p);break;
        case WifiTrialRecovery::StayInPortal:
            WiFi.disconnect(false);state=WIFI_ST_IDLE;status_str="SETUP AP";break;
    }

}

static void trial_loop() {
    unsigned long elapsed = millis() - trial_start_ms;

    if (wifi_trial_has_address(WiFi.status()==WL_CONNECTED,WiFi.localIP()!=IPAddress((uint32_t)0))) {
        set_ip_from_sta();
        trial_state = WIFI_TRIAL_CONNECTED;
        trial_active = false;
        state = WIFI_ST_CONNECTED;
        status_str = "CONNECTED";
        first_failure_ms = 0;
        snprintf(trial_detail, sizeof(trial_detail), "Connected. IP %s", ip_buf);
        Serial.printf("[WIFI] Trial succeeded, IP %s\n", ip_buf);

        // Only now do the credentials touch NVS
        if(!persist_trial(trial_params)) { trial_state=WIFI_TRIAL_FAILED_SAVE;
            snprintf(trial_detail,sizeof(trial_detail),"persistence_failed"); }

        // Leave the AP up briefly so the phone can see the success before the
        // radio follows the STA channel and drops it.
        if (portal_up) portal_shutdown_at_ms = millis() + WIFI_AP_SHUTDOWN_GRACE_MS;
        return;
    }

    if (assoc_done && trial_state == WIFI_TRIAL_ASSOCIATING) {
        // Associated and authenticated at the link layer; waiting on DHCP
        trial_state = WIFI_TRIAL_AUTHENTICATING;
    }

    int reason = last_disconnect_reason;
    if (reason != 0) {
        WifiTrialState cls = classify_reason(reason);
        // The supplicant retries on its own, so wait for a couple of consistent
        // failures before calling it rather than reacting to one stray event.
        bool decisive = (cls != WIFI_TRIAL_FAILED_TIMEOUT) &&
                        (disconnect_count >= 3 || elapsed > 12000);
        if (decisive) {
            trial_finish_failure(cls, reason);
            return;
        }
    }

    if (elapsed > WIFI_CONNECT_TIMEOUT_MS) {
        WifiTrialState cls = (reason != 0) ? classify_reason(reason) : WIFI_TRIAL_FAILED_TIMEOUT;
        trial_finish_failure(cls, reason);
    }
}

// ---------------------------------------------------------------------------
// Scanning

bool wifi_scan_start() {
    if (scan_running) return false;
    // Scanning requires the STA interface; keep the AP up if the portal is live
    if (portal_up) {
        WiFi.mode(WIFI_AP_STA);
    } else if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
    }
    int rc = WiFi.scanNetworks(true /* async */, true /* show hidden */);
    if (rc == WIFI_SCAN_FAILED) {
        Serial.println("[WIFI] Scan failed to start");
        return false;
    }
    scan_running = true;
    Serial.println("[WIFI] Scan started");
    return true;
}

// Fold the raw scan into the cache: one row per SSID, strongest RSSI wins.
static void scan_collect() {
    int n = WiFi.scanComplete();
    if (n < 0) return;

    scan_cache_count = 0;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;

        int existing = -1;
        for (int j = 0; j < scan_cache_count; j++) {
            if (ssid.equals(scan_cache[j].ssid)) { existing = j; break; }
        }

        int8_t rssi = (int8_t)WiFi.RSSI(i);
        if (existing >= 0) {
            if (rssi > scan_cache[existing].rssi) {
                scan_cache[existing].rssi = rssi;
                scan_cache[existing].channel = (uint8_t)WiFi.channel(i);
                scan_cache[existing].enc = (uint8_t)WiFi.encryptionType(i);
                scan_cache[existing].enterprise = auth_mode_is_enterprise(scan_cache[existing].enc);
            }
            continue;
        }
        if (scan_cache_count >= WIFI_SCAN_MAX) continue;

        WifiScanEntry& e = scan_cache[scan_cache_count++];
        strncpy(e.ssid, ssid.c_str(), sizeof(e.ssid) - 1);
        e.ssid[sizeof(e.ssid) - 1] = '\0';
        e.rssi = rssi;
        e.channel = (uint8_t)WiFi.channel(i);
        e.enc = (uint8_t)WiFi.encryptionType(i);
        e.enterprise = auth_mode_is_enterprise(e.enc);
    }

    // Strongest first
    for (int i = 1; i < scan_cache_count; i++) {
        WifiScanEntry key = scan_cache[i];
        int j = i - 1;
        while (j >= 0 && scan_cache[j].rssi < key.rssi) {
            scan_cache[j + 1] = scan_cache[j];
            j--;
        }
        scan_cache[j + 1] = key;
    }

    WiFi.scanDelete();
    scan_running = false;
    scan_completed_ms = millis();
    Serial.printf("[WIFI] Scan complete: %d networks (%d raw)\n", scan_cache_count, n);

    if (boot_connect_pending && config_has_wifi()) {
        WifiTrialParams p;
        params_from_config(p);
        boot_connect_pending = false;
        Serial.printf("[WIFI] Connecting to '%s' (%s)...\n", p.ssid,
                      p.security == 1 ? "WPA2-Enterprise" : "personal/open");
        start_attempt(p);
    }
}

// ---------------------------------------------------------------------------
// Main loop

void wifi_loop() {
    if (portal_up) captive_portal_loop();

    // Async scan bookkeeping
    if (scan_running) {
        int rc = WiFi.scanComplete();
        if (rc >= 0) {
            scan_collect();
        } else if (rc == WIFI_SCAN_FAILED) {
            scan_running = false;
            if (boot_connect_pending && config_has_wifi()) {
                WifiTrialParams p;
                params_from_config(p);
                boot_connect_pending = false;
                start_attempt(p);
            }
        }
    }

    // Deferred AP teardown after a successful trial
    if (portal_up && portal_shutdown_at_ms != 0 && (long)(millis() - portal_shutdown_at_ms) >= 0) {
        portal_stop();
    }

    // Portal-requested trial takes priority over the retry loop
    if (trial_requested && !trial_active) {
        trial_begin();
        return;
    }
    if (trial_active) {
        trial_loop();
        return;
    }

    // On boot, let the one-shot cache scan finish before beginning the saved
    // association. This is the only automatic scan in normal operation.
    if (boot_connect_pending) return;

    // --- Normal connection management ---

    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress((uint32_t)0)) {
        if (state != WIFI_ST_CONNECTED) {
            set_ip_from_sta();
            Serial.printf("[WIFI] Connected! IP: %s, RSSI: %d dBm\n", ip_buf, WiFi.RSSI());
            state = WIFI_ST_CONNECTED;
            status_str = "CONNECTED";
            first_failure_ms = 0;
            if (portal_up && portal_shutdown_at_ms == 0) {
                portal_shutdown_at_ms = millis() + WIFI_AP_SHUTDOWN_GRACE_MS;
            }
        }
        return;
    }

    if (state == WIFI_ST_CONNECTED) {
        Serial.println("[WIFI] Connection lost, will retry");
        state = WIFI_ST_RETRY_WAIT;
        status_str = "RECONNECTING";
        retry_wait_start_ms = millis();
        first_failure_ms = millis();
        if (trial_state == WIFI_TRIAL_CONNECTED) trial_state = WIFI_TRIAL_IDLE;
    }

    if (!config_has_wifi()) {
        // Nothing to retry; the portal is the only way forward
        if (!portal_up) portal_start();
        state = WIFI_ST_SETUP_AP;
        status_str = "SETUP AP";
        return;
    }

    if (first_failure_ms == 0) first_failure_ms = millis();

    // Unconditional fallback: after two minutes of failure the portal comes up
    // and stays up until something actually connects.
    if (!portal_up && (millis() - first_failure_ms > WIFI_SETUP_AP_AFTER_MS)) {
        Serial.println("[WIFI] No connection for 120s — bringing up setup portal");
        portal_start();
    }

    // Stop an ordinary in-flight retry when a phone joins the AP. Merely
    // suppressing the next retry is not enough: the current association can
    // still retune the shared radio and kick the phone off mid-form.
    if (portal_up && WiFi.softAPgetStationNum() > 0 && state == WIFI_ST_CONNECTING) {
        WiFi.disconnect(false);
        state = WIFI_ST_RETRY_WAIT;
        retry_wait_start_ms = millis();
        status_str = "SETUP AP";
        return;
    }

    if (state == WIFI_ST_CONNECTING) {
        if (millis() - attempt_start_ms > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.printf("[WIFI] Connection timeout (last reason %d)\n", last_disconnect_reason);
            state = WIFI_ST_RETRY_WAIT;
            status_str = portal_up ? "SETUP AP" : "TIMEOUT";
            retry_wait_start_ms = millis();
        }
        return;
    }

    // Retry pause: re-associating retunes the shared radio and kicks every phone
    // off the setup AP, so hold off while someone is on the portal.
    if (portal_up && WiFi.softAPgetStationNum() > 0) {
        status_str = "SETUP AP";
        return;
    }

    if (state != WIFI_ST_CONNECTING &&
        (millis() - retry_wait_start_ms > WIFI_RETRY_INTERVAL_MS)) {
        WifiTrialParams p;
        params_from_config(p);
        Serial.printf("[WIFI] Retrying connection to '%s'...\n", p.ssid);
        start_attempt(p);
    }
}

// ---------------------------------------------------------------------------
// Accessors

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

WifiState wifi_get_state() {
    return state;
}

bool wifi_is_ap_mode() {
    return portal_up;
}

const char* wifi_get_ap_ip() {
    return ap_ip_buf;
}

const char* wifi_get_ap_ssid() {
    return SETUP_AP_SSID;
}

int wifi_ap_station_count() {
    return portal_up ? (int)WiFi.softAPgetStationNum() : 0;
}

bool wifi_scan_in_progress() {
    return scan_running;
}

int wifi_scan_count() {
    return scan_cache_count;
}

const WifiScanEntry* wifi_scan_get(int index) {
    if (index < 0 || index >= scan_cache_count) return NULL;
    return &scan_cache[index];
}

unsigned long wifi_scan_age_ms() {
    if (scan_completed_ms == 0) return 0;
    return millis() - scan_completed_ms;
}

WifiTrialState wifi_trial_get_state() {
    return trial_state;
}

const char* wifi_trial_status_str() {
    switch (trial_state) {
        case WIFI_TRIAL_FAILED_SAVE: return "failed_save";
        case WIFI_TRIAL_ASSOCIATING:    return "associating";
        case WIFI_TRIAL_AUTHENTICATING: return "authenticating";
        case WIFI_TRIAL_CONNECTED:      return "connected";
        case WIFI_TRIAL_FAILED_AUTH:    return "failed_auth";
        case WIFI_TRIAL_FAILED_NO_AP:   return "failed_no_ap";
        case WIFI_TRIAL_FAILED_TIMEOUT: return "failed_timeout";
        default:                        return "idle";
    }
}

const char* wifi_trial_detail() {
    return trial_detail;
}

const char* wifi_trial_ssid() {
    return trial_params.ssid;
}
