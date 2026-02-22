#include "time_engine.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include <Arduino.h>
#include <time.h>
#include <Wire.h>

// DS1307 RTC I2C address
#define DS1307_ADDR 0x68

#define NTP_RESYNC_INTERVAL_MS (6UL * 60 * 60 * 1000)  // 6 hours

static bool ntp_synced = false;
static bool rtc_available = false;
static unsigned long last_ntp_sync_ms = 0;
static unsigned long boot_time_sec = 0;
static unsigned long boot_millis = 0;

// BCD conversion helpers
static uint8_t bcd_to_dec(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static uint8_t dec_to_bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

// Try to detect DS1307 RTC on I2C bus
static bool rtc_detect() {
    Wire.beginTransmission(DS1307_ADDR);
    return (Wire.endTransmission() == 0);
}

// Write current system time to DS1307
static void rtc_write_time() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) return;

    Wire.beginTransmission(DS1307_ADDR);
    Wire.write(0x00); // register 0
    Wire.write(dec_to_bcd(timeinfo.tm_sec));
    Wire.write(dec_to_bcd(timeinfo.tm_min));
    Wire.write(dec_to_bcd(timeinfo.tm_hour));
    Wire.write(dec_to_bcd(timeinfo.tm_wday + 1));
    Wire.write(dec_to_bcd(timeinfo.tm_mday));
    Wire.write(dec_to_bcd(timeinfo.tm_mon + 1));
    Wire.write(dec_to_bcd(timeinfo.tm_year - 100));
    Wire.endTransmission();

    Serial.println("[TIME] Written to RTC");
}

// Read time from DS1307 and set system time
static bool rtc_read_time() {
    Wire.beginTransmission(DS1307_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;

    Wire.requestFrom(DS1307_ADDR, 7);
    if (Wire.available() < 7) return false;

    uint8_t sec = bcd_to_dec(Wire.read() & 0x7F);
    uint8_t min = bcd_to_dec(Wire.read());
    uint8_t hour = bcd_to_dec(Wire.read() & 0x3F);
    Wire.read(); // day of week
    uint8_t day = bcd_to_dec(Wire.read());
    uint8_t month = bcd_to_dec(Wire.read());
    uint8_t year = bcd_to_dec(Wire.read());

    struct tm t;
    t.tm_sec = sec;
    t.tm_min = min;
    t.tm_hour = hour;
    t.tm_mday = day;
    t.tm_mon = month - 1;
    t.tm_year = year + 100;
    t.tm_isdst = -1;

    time_t epoch = mktime(&t);
    if (epoch < 1600000000) return false; // sanity check

    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    Serial.printf("[TIME] Read from RTC: %02d:%02d:%02d\n", hour, min, sec);
    return true;
}

static void ntp_sync() {
    AppConfig& cfg = config_get();

    configTzTime(cfg.timezone, "pool.ntp.org", "time.nist.gov", "time.google.com");

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
        ntp_synced = true;
        last_ntp_sync_ms = millis();
        Serial.printf("[TIME] NTP synced: %02d:%02d:%02d\n",
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        // Write to RTC if available
        if (rtc_available) {
            rtc_write_time();
        }
    } else {
        Serial.println("[TIME] NTP sync failed");
    }
}

void time_init() {
    boot_millis = millis();

    // Init I2C
    Wire.begin(21, 22);

    // Detect RTC
    rtc_available = rtc_detect();
    if (rtc_available) {
        Serial.println("[TIME] DS1307 RTC detected");
        // Read time from RTC as initial source
        if (rtc_read_time()) {
            Serial.println("[TIME] Using RTC time until NTP sync");
        }
    } else {
        Serial.println("[TIME] No RTC detected");
    }

    // Try NTP if WiFi is connected
    if (wifi_is_connected()) {
        ntp_sync();
    }
}

void time_loop() {
    // Try NTP sync if not yet synced and WiFi just became available
    if (!ntp_synced && wifi_is_connected()) {
        ntp_sync();
    }

    // Periodic NTP resync
    if (ntp_synced && wifi_is_connected() &&
        (millis() - last_ntp_sync_ms > NTP_RESYNC_INTERVAL_MS)) {
        Serial.println("[TIME] NTP resync");
        ntp_sync();
    }
}

bool time_is_available() {
    struct tm timeinfo;
    return getLocalTime(&timeinfo, 10);
}

int time_get_hour() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return timeinfo.tm_hour;
    }
    return -1;
}

int time_get_minute() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return timeinfo.tm_min;
    }
    return -1;
}

int time_get_second() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return timeinfo.tm_sec;
    }
    return -1;
}

void time_get_string(char* buf, int bufsize, bool use_24h) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) {
        snprintf(buf, bufsize, "--:--");
        return;
    }

    int h = timeinfo.tm_hour;
    if (!use_24h) {
        h = h % 12;
        if (h == 0) h = 12;
    }
    snprintf(buf, bufsize, "%d:%02d", h, timeinfo.tm_min);
}

int time_get_day() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return timeinfo.tm_mday;
    }
    return -1;
}

int time_get_month() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return timeinfo.tm_mon + 1;  // tm_mon is 0-11
    }
    return -1;
}

int time_get_weekday() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return timeinfo.tm_wday;  // 0=Sun
    }
    return -1;
}

static const char* MONTH_ABBRS[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

const char* time_get_month_abbr() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return MONTH_ABBRS[timeinfo.tm_mon];
    }
    return "???";
}

unsigned long time_get_uptime_sec() {
    return (millis() - boot_millis) / 1000;
}
