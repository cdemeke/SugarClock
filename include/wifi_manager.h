#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include <stddef.h>

// Connection state machine.
//
//   IDLE -> CONNECTING -> CONNECTED
//              |  \-> RETRY_WAIT -> CONNECTING
//              \-> SETUP_AP  (portal up, STA keeps retrying underneath)
//
// SETUP_AP is entered automatically after WIFI_SETUP_AP_AFTER_MS of continuous
// failure, and on boot when no credentials are stored at all. It is left only
// when a connection actually succeeds.
enum WifiState {
    WIFI_ST_IDLE,
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,
    WIFI_ST_RETRY_WAIT,
    WIFI_ST_SETUP_AP
};

// Progress of a portal-initiated trial connection. These are the exact strings
// reported by GET /api/wifi/status.
enum WifiTrialState {
    WIFI_TRIAL_IDLE,
    WIFI_TRIAL_ASSOCIATING,
    WIFI_TRIAL_AUTHENTICATING,
    WIFI_TRIAL_CONNECTED,
    WIFI_TRIAL_FAILED_AUTH,
    WIFI_TRIAL_FAILED_NO_AP,
    WIFI_TRIAL_FAILED_SAVE,
    WIFI_TRIAL_FAILED_TIMEOUT
};

// Credentials for a trial connection. Nothing here is written to NVS unless the
// trial succeeds.
struct WifiTrialParams {
    char ssid[64];
    int  security;       // 0 = personal/open, 1 = WPA2-Enterprise
    int  eap_method;     // 0 = PEAP, 1 = EAP-TTLS
    char identity[128];  // 802.1X username (enterprise only)
    char password[128];  // PSK or 802.1X password
    char anon_identity[128];
    bool validate_ca;    // validate the RADIUS server with /wifi_ca.pem
};

// One entry of the cached scan
struct WifiScanEntry {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t enc;        // raw wifi_auth_mode_t
    bool enterprise;    // derived: enc == WIFI_AUTH_WPA2_ENTERPRISE (or WPA3-ent)
};

#define WIFI_SCAN_MAX 24

// Initialize WiFi connection manager
void wifi_init();

// Non-blocking WiFi loop - handles connection/reconnection, the setup AP and trials
void wifi_loop();

// Check if currently connected
bool wifi_is_connected();

// Get IP address as string
const char* wifi_get_ip();

// Get RSSI (signal strength)
int wifi_get_rssi();

// Get WiFi status string
const char* wifi_get_status();

// Current state machine state
WifiState wifi_get_state();

// True while the setup portal AP is running
bool wifi_is_ap_mode();

// Get AP mode IP address as string
const char* wifi_get_ap_ip();

// Get the setup AP SSID (the AP is intentionally open — no passphrase)
const char* wifi_get_ap_ssid();

// Number of phones currently associated to the setup AP
int wifi_ap_station_count();

// --- Scanning (cached; never polled in the background) ---

// Kick off an asynchronous scan. Returns false if one is already running.
bool wifi_scan_start();

// True while an async scan is in flight
bool wifi_scan_in_progress();

// Number of cached results
int wifi_scan_count();

// Cached result by index, or NULL if out of range
const WifiScanEntry* wifi_scan_get(int index);

// millis() timestamp of the last completed scan (0 if never)
unsigned long wifi_scan_age_ms();

// --- Trial connections driven by the setup portal ---

// Queue a trial connection. Safe to call from an async web handler: the work
// happens on the next wifi_loop() pass, so the handler never blocks.
bool wifi_trial_start(const WifiTrialParams& params);

// Current trial state
WifiTrialState wifi_trial_get_state();

// Trial state as the wire string ("idle", "associating", ... "connected")
const char* wifi_trial_status_str();

// Human-readable detail for the last failure ("" when there is none)
const char* wifi_trial_detail();

// SSID of the trial in progress or last attempted
const char* wifi_trial_ssid();

#endif // WIFI_MANAGER_H
