#include "net_check.h"
#include "http_client.h"
#include "ble_manager.h"
#include "config_manager.h"
#include "wifi_manager.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include <atomic>

#define NTP_PROBE_HOST     "pool.ntp.org"
#define NTP_PORT           123
#define NTP_TIMEOUT_MS     3000
#define TLS_TIMEOUT_MS     6000
// Delay after a fresh connection before probing, so DHCP and the DNS server
// handed out by the network have settled.
#define SETTLE_MS          4000
// Cheap enough to redo periodically; a school firewall can change under us.
#define RERUN_INTERVAL_MS  (15UL * 60 * 1000)

enum Step {
    STEP_IDLE,
    STEP_WAIT_SETTLE,
    STEP_DNS,
    STEP_DATA,
    STEP_NTP,
    STEP_DONE
};

static Step step = STEP_IDLE;
static unsigned long step_ready_ms = 0;
static bool was_connected = false;

static NetCheckResult res_dns = NC_UNKNOWN;
static NetCheckResult res_data = NC_UNKNOWN;
static NetCheckResult res_ntp = NC_UNKNOWN;
static unsigned long last_run_ms = 0;
static char summary[128] = "";
static char data_host[96] = "";
static std::atomic<bool> configuration_changed{false};
static unsigned long observed_fetch_generation = 0;
static bool provider_response_ok = false;

// Pull the bare hostname out of the configured data source.
static void resolve_data_host() {
    AppConfig& cfg = config_get();
    data_host[0] = '\0';

    if (cfg.data_source == 2) {
        return; // demo mode synthesises readings; nothing to reach
    }
    if (cfg.data_source == 1) {
        strncpy(data_host, cfg.dexcom_us ? "share2.dexcom.com" : "shareous1.dexcom.com",
                sizeof(data_host) - 1);
        return;
    }

    // Custom URL / Nightscout: strip scheme, path, port and credentials
    const char* url = cfg.server_url;
    if (url[0] == '\0') return;
    const char* p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t authority_len=strcspn(p,"/?#");
    const char* at=static_cast<const char*>(memchr(p,'@',authority_len));
    if(at) p=at+1;

    size_t i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && p[i] != '?' && p[i] != '#' && i < sizeof(data_host) - 1) {
        data_host[i] = p[i];
        i++;
    }
    data_host[i] = '\0';
}

static void build_summary() {
    if (res_dns == NC_FAIL) {
        snprintf(summary, sizeof(summary),
                 "Connected, but DNS is not answering. The network may need a sign-in page.");
    } else if (res_data == NC_FAIL) {
        snprintf(summary, sizeof(summary),
                 "Connected, but cannot reach %s. Check the source and network.",
                 data_host[0] ? data_host : "the data source");
    } else if (res_ntp == NC_FAIL) {
        snprintf(summary, sizeof(summary),
                 "Connected and data works, but NTP (UDP 123) is blocked, so the clock may drift.");
    } else if (res_dns == NC_OK && res_data == NC_OK && res_ntp == NC_OK) {
        snprintf(summary, sizeof(summary), "Connected. DNS, data source and time all reachable.");
    } else if (res_dns == NC_OK && res_data == NC_OK) {
        snprintf(summary, sizeof(summary), "Connected. Data source reachable; time check pending.");
    } else {
        summary[0] = '\0';
    }
}

static bool probe_dns() {
    const char* host = data_host[0] ? data_host : "pool.ntp.org";
    IPAddress addr;
    bool ok = WiFi.hostByName(host, addr);
    Serial.printf("[NETCHK] DNS %s -> %s\n", host, ok ? addr.toString().c_str() : "FAILED");
    return ok;
}

static bool probe_data() {
    if (data_host[0] == '\0') return true; // demo mode / nothing configured

    WiFiClientSecure client;
    client.setInsecure();          // reachability only, not a trust decision
    client.setTimeout(TLS_TIMEOUT_MS / 1000);
    bool ok = client.connect(data_host, 443, TLS_TIMEOUT_MS);
    client.stop();
    Serial.printf("[NETCHK] HTTPS %s:443 -> %s\n", data_host, ok ? "ok" : "FAILED");
    return ok;
}

static bool probe_ntp() {
    IPAddress addr;
    if (!WiFi.hostByName(NTP_PROBE_HOST, addr)) {
        Serial.println("[NETCHK] NTP host did not resolve");
        return false;
    }

    WiFiUDP udp;
    if (!udp.begin(0)) return false;

    uint8_t packet[48];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0b00100011; // LI = 0, VN = 4, Mode = 3 (client)

    udp.beginPacket(addr, NTP_PORT);
    udp.write(packet, sizeof(packet));
    udp.endPacket();

    unsigned long start = millis();
    while (millis() - start < NTP_TIMEOUT_MS) {
        if (udp.parsePacket() >= 48) {
            udp.stop();
            Serial.println("[NETCHK] NTP ok");
            return true;
        }
        delay(20);
    }
    udp.stop();
    Serial.println("[NETCHK] NTP FAILED (UDP 123 likely blocked)");
    return false;
}

void netcheck_init() {
    step = STEP_IDLE;
    res_dns = res_data = res_ntp = NC_UNKNOWN;
    summary[0] = '\0';
    was_connected = false;
    last_run_ms = 0;
    observed_fetch_generation = http_fetch_generation();
    provider_response_ok = false;
    configuration_changed = false;
}

void netcheck_configuration_changed() { configuration_changed = true; }

void netcheck_request() {
    if (!wifi_is_connected()) return;
    resolve_data_host();
    res_dns = res_data = res_ntp = NC_UNKNOWN;
    if (provider_response_ok) res_dns = res_data = NC_OK;
    summary[0] = '\0';
    step = STEP_WAIT_SETTLE;
    step_ready_ms = millis() + SETTLE_MS;
}

void netcheck_loop() {
    bool connected = wifi_is_connected();

    // A response from the previous source must not establish reachability for
    // replacement settings. http_loop discards any in-flight old-source result.
    if (configuration_changed.exchange(false)) {
        observed_fetch_generation = http_fetch_generation();
        provider_response_ok = false;
        res_dns = res_data = res_ntp = NC_UNKNOWN;
        summary[0] = '\0';
        step = STEP_IDLE;
        was_connected = connected;
        if (connected) netcheck_request();
        return;
    }

    if (connected && !was_connected) {
        was_connected = true;
        observed_fetch_generation = http_fetch_generation();
        provider_response_ok = false;
        netcheck_request();
        return;
    }
    if (!connected) {
        was_connected = false;
        if (step != STEP_IDLE) step = STEP_IDLE;
        return;
    }

    // A real provider response is stronger evidence than an earlier probe. In
    // particular, a failed startup probe must not hide recovered glucose for
    // the 15-minute diagnostic interval. HTTP authentication errors also prove
    // connectivity, but do not claim that credentials or readings are valid.
    const unsigned long generation = http_fetch_generation();
    if (generation != observed_fetch_generation) {
        observed_fetch_generation = generation;
        const AppConfig& cfg = config_get();
        provider_response_ok = cfg.glucose_enabled && cfg.data_source != 2 &&
                               http_get_last_response_code() > 0;
        if (provider_response_ok) {
            res_dns = res_data = NC_OK;
            if (step == STEP_DNS || step == STEP_DATA) step = STEP_NTP;
            if (step == STEP_DONE || step == STEP_IDLE) build_summary();
        }
    }
    if (step == STEP_DONE || step == STEP_IDLE) {
        if (last_run_ms != 0 && millis() - last_run_ms > RERUN_INTERVAL_MS) {
            netcheck_request();
        }
        return;
    }

    if (step == STEP_WAIT_SETTLE) {
        if ((long)(millis() - step_ready_ms) < 0) return;
        step = provider_response_ok ? STEP_NTP : STEP_DNS;
        return;
    }

    // One probe per pass: each blocks for a few seconds and the watchdog is 30s,
    // so they must not be chained inside a single loop iteration.
    switch (step) {
        case STEP_DNS:
            res_dns = probe_dns() ? NC_OK : NC_FAIL;
            step = (res_dns == NC_OK) ? STEP_DATA : STEP_DONE;
            break;
        case STEP_DATA:
            // An HTTP response already proves provider reachability, including
            // an authentication rejection. Avoid a redundant TLS handshake.
            if(provider_response_ok) res_data=NC_OK;
            else {
                if(!ble_acquire_network()) return;
                res_data = probe_data() ? NC_OK : NC_FAIL;
                ble_release_network();
            }
            step = STEP_NTP;
            break;
        case STEP_NTP:
            res_ntp = probe_ntp() ? NC_OK : NC_FAIL;
            step = STEP_DONE;
            break;
        default:
            break;
    }

    if (step == STEP_DONE) {
        last_run_ms = millis();
        build_summary();
        if (summary[0]) Serial.printf("[NETCHK] %s\n", summary);
    }
}

bool netcheck_running() {
    return step != STEP_IDLE && step != STEP_DONE;
}

NetCheckResult netcheck_dns()  { return res_dns; }
NetCheckResult netcheck_data() { return res_data; }
NetCheckResult netcheck_ntp()  { return res_ntp; }

const char* netcheck_data_host() { return data_host; }
const char* netcheck_summary()   { return summary; }

bool netcheck_has_problem() {
    return res_dns == NC_FAIL || res_data == NC_FAIL || res_ntp == NC_FAIL;
}

unsigned long netcheck_last_run_ms() { return last_run_ms; }
