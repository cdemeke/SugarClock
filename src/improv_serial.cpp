#include "improv_serial.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <Arduino.h>

// Improv Serial Protocol v1
// https://www.improv-wifi.com/serial/

#define IMPROV_SERIAL_VERSION 1

// Packet types
#define TYPE_CURRENT_STATE 0x01
#define TYPE_ERROR_STATE   0x02
#define TYPE_RPC_COMMAND   0x03
#define TYPE_RPC_RESULT    0x04

// States
#define STATE_READY        0x02
#define STATE_PROVISIONING 0x03
#define STATE_PROVISIONED  0x04

// Error codes
#define ERROR_NONE              0x00
#define ERROR_INVALID_RPC       0x01
#define ERROR_UNKNOWN_RPC       0x02
#define ERROR_UNABLE_TO_CONNECT 0x03
#define ERROR_NOT_AUTHORIZED    0x04

// RPC commands
#define CMD_WIFI_SETTINGS  0x01
#define CMD_IDENTIFY       0x02

#define IMPROV_BUF_SIZE 256
#define IMPROV_HEADER_LEN 9  // "IMPROV" (6) + version (1) + type (1) + length (1)
#define WIFI_CONNECT_TIMEOUT 15000

static uint8_t rx_buf[IMPROV_BUF_SIZE];
static int rx_pos = 0;
static bool active = false;

static const uint8_t HEADER[] = {'I', 'M', 'P', 'R', 'O', 'V'};

static void send_packet(uint8_t type, const uint8_t* data, uint8_t len) {
    uint8_t packet[IMPROV_BUF_SIZE];
    int pos = 0;

    // Header
    for (int i = 0; i < 6; i++) packet[pos++] = HEADER[i];
    packet[pos++] = IMPROV_SERIAL_VERSION;
    packet[pos++] = type;
    packet[pos++] = len;

    // Data
    for (int i = 0; i < len; i++) packet[pos++] = data[i];

    // Checksum (sum of all bytes from type onward)
    uint8_t checksum = 0;
    for (int i = 6; i < pos; i++) checksum += packet[i];
    packet[pos++] = checksum;

    Serial.write(packet, pos);
}

static void send_state(uint8_t state) {
    send_packet(TYPE_CURRENT_STATE, &state, 1);
}

static void send_error(uint8_t error) {
    send_packet(TYPE_ERROR_STATE, &error, 1);
}

static void send_rpc_result(uint8_t command, const char* url) {
    uint8_t data[IMPROV_BUF_SIZE];
    int pos = 0;

    data[pos++] = command;

    if (url && strlen(url) > 0) {
        // Number of strings
        uint8_t url_len = strlen(url);
        data[pos++] = 1;         // 1 string follows
        data[pos++] = url_len;   // length of the string
        memcpy(data + pos, url, url_len);
        pos += url_len;
    } else {
        data[pos++] = 0; // no strings
    }

    send_packet(TYPE_RPC_RESULT, data, pos);
}

static void handle_wifi_settings(const uint8_t* data, uint8_t len) {
    if (len < 2) {
        send_error(ERROR_INVALID_RPC);
        return;
    }

    int pos = 0;

    // Read SSID (length-prefixed string)
    uint8_t ssid_len = data[pos++];
    if (!ssid_len || ssid_len > 32 || pos + ssid_len > len) {
        send_error(ERROR_INVALID_RPC);
        return;
    }
    char ssid[64] = {0};
    memcpy(ssid, data + pos, ssid_len);
    ssid[ssid_len] = '\0';
    pos += ssid_len;

    // Read password (length-prefixed string)
    if (pos >= len) {
        send_error(ERROR_INVALID_RPC);
        return;
    }
    uint8_t pass_len = data[pos++];
    if (pass_len > 63 || pos + pass_len > len) {
        send_error(ERROR_INVALID_RPC);
        return;
    }
    char password[64] = {0};
    memcpy(password, data + pos, pass_len);
    password[pass_len] = '\0';

    WifiTrialParams params = {};
    strncpy(params.ssid,ssid,sizeof(params.ssid)-1);
    strncpy(params.password,password,sizeof(params.password)-1);
    // Improv explicitly selects personal/open Wi-Fi; glucose fields are untouched.
    if(!wifi_trial_start(params)) { send_error(ERROR_UNABLE_TO_CONNECT);return; }
    send_state(STATE_PROVISIONING);
    active=true;

}

static void handle_identify() {
    Serial.println("[IMPROV] Identify requested");
    // No-op — we could blink the display here if desired
    send_rpc_result(CMD_IDENTIFY, NULL);
}

static void process_packet() {
    // Verify header
    for (int i = 0; i < 6; i++) {
        if (rx_buf[i] != HEADER[i]) return;
    }

    uint8_t version = rx_buf[6];
    if (version != IMPROV_SERIAL_VERSION) return;

    uint8_t type = rx_buf[7];
    uint8_t data_len = rx_buf[8];

    // Verify we have enough data + checksum
    if (rx_pos < IMPROV_HEADER_LEN + data_len + 1) return;

    // Verify checksum
    uint8_t checksum = 0;
    for (int i = 6; i < IMPROV_HEADER_LEN + data_len; i++) {
        checksum += rx_buf[i];
    }
    if (checksum != rx_buf[IMPROV_HEADER_LEN + data_len]) {
        Serial.println("[IMPROV] Checksum mismatch");
        return;
    }

    uint8_t* data = rx_buf + IMPROV_HEADER_LEN;

    if (type == TYPE_RPC_COMMAND) {
        if (data_len < 1) {
            send_error(ERROR_INVALID_RPC);
            return;
        }

        uint8_t command = data[0];
        uint8_t cmd_data_len = (data_len > 1) ? data[1] : 0;
        uint8_t* cmd_data = data + 2;

        if(data_len<2 || cmd_data_len>data_len-2) { send_error(ERROR_INVALID_RPC);return; }
        switch (command) {
            case CMD_WIFI_SETTINGS:
                handle_wifi_settings(cmd_data, cmd_data_len);
                break;
            case CMD_IDENTIFY:
                handle_identify();
                break;
            default:
                send_error(ERROR_UNKNOWN_RPC);
                break;
        }
    }
}

void improv_init() {
    Serial.println("[IMPROV] Improv Wi-Fi serial handler ready");
}

void improv_loop() {
    if(active) {
        WifiTrialState state=wifi_trial_get_state();
        if(state==WIFI_TRIAL_CONNECTED) {
            char url[64];snprintf(url,sizeof(url),"http://%s",wifi_get_ip());
            send_rpc_result(CMD_WIFI_SETTINGS,url);send_state(STATE_PROVISIONED);active=false;
        } else if(state>=WIFI_TRIAL_FAILED_AUTH) {
            send_error(ERROR_UNABLE_TO_CONNECT);send_state(STATE_READY);active=false;
        }
    }

    // Run Improv when WiFi is not configured, OR for the first 2 minutes
    // after boot. The boot window allows the web installer to send WiFi
    // credentials even on reinstalls (where the erase prompt is skipped
    // and old config may persist).
    if (config_has_wifi() && !active && millis() > 120000) return;

    // Periodically announce ready state so ESP Web Tools detects us
    static unsigned long last_announce = 0;
    if (millis() - last_announce > 1000) {
        last_announce = millis();
        if (!active) {
            send_state(STATE_READY);
        }
    }

    // Read incoming serial data
    while (Serial.available()) {
        uint8_t b = Serial.read();

        // Look for header start
        if (rx_pos < 6) {
            if (b == HEADER[rx_pos]) {
                rx_buf[rx_pos++] = b;
            } else {
                rx_pos = 0;
                if (b == HEADER[0]) {
                    rx_buf[rx_pos++] = b;
                }
            }
            continue;
        }

        rx_buf[rx_pos++] = b;

        // Once we have the header + type + length, check if packet is complete
        if (rx_pos >= IMPROV_HEADER_LEN) {
            uint8_t data_len = rx_buf[8];
            int total_len = IMPROV_HEADER_LEN + data_len + 1; // +1 for checksum

            if (rx_pos >= total_len) {
                process_packet();
                rx_pos = 0;
            }
        }

        // Overflow protection
        if (rx_pos >= IMPROV_BUF_SIZE) {
            rx_pos = 0;
        }
    }
}

bool improv_is_active() {
    return active;
}
