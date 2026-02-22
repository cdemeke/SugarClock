#include "config_manager.h"
#include <Preferences.h>
#include <Arduino.h>

#define CONFIG_NAMESPACE "tc001cfg"
#define CONFIG_MAGIC     0x474C5543  // "GLUC"

static AppConfig config;
static Preferences prefs;

static void config_set_defaults() {
    memset(&config, 0, sizeof(AppConfig));

    // WiFi
    strncpy(config.wifi_ssid, "tight*5", sizeof(config.wifi_ssid));
    strncpy(config.wifi_password, "redcat153", sizeof(config.wifi_password));

    // Data source
    config.data_source = 0; // custom URL by default

    // Custom server
    config.server_url[0] = '\0';
    config.auth_token[0] = '\0';

    // Dexcom Share
    config.dexcom_username[0] = '\0';
    config.dexcom_password[0] = '\0';
    config.dexcom_us = true;

    config.poll_interval_sec = 60;

    // Display
    config.brightness = 40;
    config.auto_brightness = true;
    config.show_delta = false;
    config.use_mmol = false;

    // Glucose thresholds
    config.thresh_urgent_low = 70;
    config.thresh_low = 80;
    config.thresh_high = 180;
    config.thresh_urgent_high = 250;

    // Time
    strncpy(config.timezone, "EST5EDT,M3.2.0,M11.1.0", sizeof(config.timezone));
    config.use_24h = false;

    // Default mode
    config.default_mode = 0; // glucose

    // Alerts
    config.alert_enabled = false;
    config.alert_low = 70;
    config.alert_high = 250;
    config.alert_snooze_min = 15;

    // Theme colors
    config.color_urgent_low  = 0xEA4335;
    config.color_low         = 0xFBBC04;
    config.color_in_range    = 0x34A853;
    config.color_high        = 0xFBBC04;
    config.color_urgent_high = 0xEA4335;

    // Clock & weather colors
    config.color_clock   = 0x00FFFF;
    config.color_weather = 0x00FFFF;

    // Night mode
    config.night_mode_enabled = false;
    config.night_start_hour = 22;
    config.night_end_hour = 7;
    config.night_brightness = 10;

    // Stale timeout
    config.stale_timeout_min = 20;

    // Weather
    config.weather_enabled = false;
    config.weather_api_key[0] = '\0';
    strncpy(config.weather_city, "New York,US", sizeof(config.weather_city));
    config.weather_use_f = true;
    config.weather_poll_min = 15;

    // Date display
    config.date_on_time_screen = true;
    config.date_format = 0;  // M/DD

    // Pomodoro timer
    config.timer_enabled = true;
    config.timer_work_min = 25;
    config.timer_break_min = 5;
    config.timer_long_break_min = 15;
    config.timer_sessions = 4;
    config.timer_buzzer = true;

    // Stopwatch
    config.stopwatch_enabled = true;

    // Notifications
    config.notify_enabled = true;
    config.notify_default_duration = 60;
    config.notify_allow_buzzer = true;

    // System monitor
    config.sysmon_enabled = true;
    strncpy(config.sysmon_label, "CPU", sizeof(config.sysmon_label));
    config.sysmon_display_mode = 0;
    config.sysmon_warn_pct = 50;
    config.sysmon_crit_pct = 80;

    // Countdown
    config.countdown_enabled = false;
    config.countdown_name[0] = '\0';
    config.countdown_target = 0;

    // Auto-cycle
    config.auto_cycle_enabled = true;
    config.auto_cycle_sec = 10;

    config.magic = CONFIG_MAGIC;
}

void config_init() {
    prefs.begin(CONFIG_NAMESPACE, false);

    // Check if config exists
    uint32_t magic = prefs.getUInt("magic", 0);

    if (magic != CONFIG_MAGIC) {
        Serial.println("[CONFIG] No valid config found, writing defaults");
        config_set_defaults();
        config_save();
    } else {
        Serial.println("[CONFIG] Loading saved config");

        prefs.getString("wifi_ssid", config.wifi_ssid, sizeof(config.wifi_ssid));
        prefs.getString("wifi_pass", config.wifi_password, sizeof(config.wifi_password));
        config.data_source = prefs.getInt("data_src", 0);
        prefs.getString("server_url", config.server_url, sizeof(config.server_url));
        prefs.getString("auth_token", config.auth_token, sizeof(config.auth_token));
        prefs.getString("dex_user", config.dexcom_username, sizeof(config.dexcom_username));
        prefs.getString("dex_pass", config.dexcom_password, sizeof(config.dexcom_password));
        config.dexcom_us = prefs.getBool("dex_us", true);
        config.poll_interval_sec = prefs.getInt("poll_int", 60);
        config.brightness = prefs.getUChar("brightness", 40);
        config.auto_brightness = prefs.getBool("auto_brt", true);
        config.show_delta = prefs.getBool("show_delta", false);
        config.use_mmol = prefs.getBool("use_mmol", false);
        config.thresh_urgent_low = prefs.getInt("t_ulow", 70);
        config.thresh_low = prefs.getInt("t_low", 80);
        config.thresh_high = prefs.getInt("t_high", 180);
        config.thresh_urgent_high = prefs.getInt("t_uhigh", 250);
        prefs.getString("timezone", config.timezone, sizeof(config.timezone));
        config.use_24h = prefs.getBool("use_24h", false);
        config.default_mode = prefs.getInt("def_mode", 0);

        // Alerts
        config.alert_enabled = prefs.getBool("alert_en", false);
        config.alert_low = prefs.getInt("alert_low", 70);
        config.alert_high = prefs.getInt("alert_high", 250);
        config.alert_snooze_min = prefs.getInt("alert_snz", 15);

        // Theme colors
        config.color_urgent_low  = prefs.getUInt("c_ulow", 0xEA4335);
        config.color_low         = prefs.getUInt("c_low", 0xFBBC04);
        config.color_in_range    = prefs.getUInt("c_inrange", 0x34A853);
        config.color_high        = prefs.getUInt("c_high", 0xFBBC04);
        config.color_urgent_high = prefs.getUInt("c_uhigh", 0xEA4335);

        // Clock & weather colors
        config.color_clock   = prefs.getUInt("c_clock", 0x00FFFF);
        config.color_weather = prefs.getUInt("c_weather", 0x00FFFF);

        // Night mode
        config.night_mode_enabled = prefs.getBool("night_en", false);
        config.night_start_hour = prefs.getInt("night_start", 22);
        config.night_end_hour = prefs.getInt("night_end", 7);
        config.night_brightness = prefs.getUChar("night_brt", 10);

        // Stale timeout
        config.stale_timeout_min = prefs.getInt("stale_min", 20);

        // Weather
        config.weather_enabled = prefs.getBool("wx_en", false);
        prefs.getString("wx_apikey", config.weather_api_key, sizeof(config.weather_api_key));
        prefs.getString("wx_city", config.weather_city, sizeof(config.weather_city));
        if (strlen(config.weather_city) == 0) {
            strncpy(config.weather_city, "New York,US", sizeof(config.weather_city));
        }
        config.weather_use_f = prefs.getBool("wx_use_f", true);
        config.weather_poll_min = prefs.getInt("wx_poll", 15);
        if (config.weather_poll_min < 5) config.weather_poll_min = 5;

        // Date display
        config.date_on_time_screen = prefs.getBool("date_en", true);
        config.date_format = prefs.getInt("date_fmt", 0);

        // Timer
        config.timer_enabled = prefs.getBool("tmr_en", true);
        config.timer_work_min = prefs.getInt("tmr_work", 25);
        config.timer_break_min = prefs.getInt("tmr_brk", 5);
        config.timer_long_break_min = prefs.getInt("tmr_lbrk", 15);
        config.timer_sessions = prefs.getInt("tmr_sess", 4);
        config.timer_buzzer = prefs.getBool("tmr_buzz", true);

        // Stopwatch
        config.stopwatch_enabled = prefs.getBool("sw_en", true);

        // Notifications
        config.notify_enabled = prefs.getBool("ntfy_en", true);
        config.notify_default_duration = prefs.getInt("ntfy_dur", 60);
        config.notify_allow_buzzer = prefs.getBool("ntfy_buzz", true);

        // System monitor
        config.sysmon_enabled = prefs.getBool("smon_en", true);
        prefs.getString("smon_lbl", config.sysmon_label, sizeof(config.sysmon_label));
        if (strlen(config.sysmon_label) == 0) {
            strncpy(config.sysmon_label, "CPU", sizeof(config.sysmon_label));
        }
        config.sysmon_display_mode = prefs.getInt("smon_dmode", 0);
        config.sysmon_warn_pct = prefs.getInt("smon_warn", 50);
        config.sysmon_crit_pct = prefs.getInt("smon_crit", 80);

        // Countdown
        config.countdown_enabled = prefs.getBool("cd_en", false);
        prefs.getString("cd_name", config.countdown_name, sizeof(config.countdown_name));
        config.countdown_target = prefs.getULong("cd_target", 0);

        // Auto-cycle
        config.auto_cycle_enabled = prefs.getBool("acyc_en", true);
        config.auto_cycle_sec = prefs.getInt("acyc_sec", 10);
        if (config.auto_cycle_sec < 3) config.auto_cycle_sec = 3;
        if (config.auto_cycle_sec > 300) config.auto_cycle_sec = 300;

        config.magic = CONFIG_MAGIC;

        // Enforce minimum poll interval
        if (config.poll_interval_sec < 15) {
            config.poll_interval_sec = 15;
        }
    }

    Serial.printf("[CONFIG] Poll interval: %ds, Brightness: %d\n",
                  config.poll_interval_sec, config.brightness);
}

void config_save() {
    prefs.putUInt("magic", CONFIG_MAGIC);
    prefs.putString("wifi_ssid", config.wifi_ssid);
    prefs.putString("wifi_pass", config.wifi_password);
    prefs.putInt("data_src", config.data_source);
    prefs.putString("server_url", config.server_url);
    prefs.putString("auth_token", config.auth_token);
    prefs.putString("dex_user", config.dexcom_username);
    prefs.putString("dex_pass", config.dexcom_password);
    prefs.putBool("dex_us", config.dexcom_us);
    prefs.putInt("poll_int", config.poll_interval_sec);
    prefs.putUChar("brightness", config.brightness);
    prefs.putBool("auto_brt", config.auto_brightness);
    prefs.putBool("show_delta", config.show_delta);
    prefs.putBool("use_mmol", config.use_mmol);
    prefs.putInt("t_ulow", config.thresh_urgent_low);
    prefs.putInt("t_low", config.thresh_low);
    prefs.putInt("t_high", config.thresh_high);
    prefs.putInt("t_uhigh", config.thresh_urgent_high);
    prefs.putString("timezone", config.timezone);
    prefs.putBool("use_24h", config.use_24h);
    prefs.putInt("def_mode", config.default_mode);

    // Alerts
    prefs.putBool("alert_en", config.alert_enabled);
    prefs.putInt("alert_low", config.alert_low);
    prefs.putInt("alert_high", config.alert_high);
    prefs.putInt("alert_snz", config.alert_snooze_min);

    // Theme colors
    prefs.putUInt("c_ulow", config.color_urgent_low);
    prefs.putUInt("c_low", config.color_low);
    prefs.putUInt("c_inrange", config.color_in_range);
    prefs.putUInt("c_high", config.color_high);
    prefs.putUInt("c_uhigh", config.color_urgent_high);

    // Clock & weather colors
    prefs.putUInt("c_clock", config.color_clock);
    prefs.putUInt("c_weather", config.color_weather);

    // Night mode
    prefs.putBool("night_en", config.night_mode_enabled);
    prefs.putInt("night_start", config.night_start_hour);
    prefs.putInt("night_end", config.night_end_hour);
    prefs.putUChar("night_brt", config.night_brightness);

    // Stale timeout
    prefs.putInt("stale_min", config.stale_timeout_min);

    // Weather
    prefs.putBool("wx_en", config.weather_enabled);
    prefs.putString("wx_apikey", config.weather_api_key);
    prefs.putString("wx_city", config.weather_city);
    prefs.putBool("wx_use_f", config.weather_use_f);
    prefs.putInt("wx_poll", config.weather_poll_min);

    // Date display
    prefs.putBool("date_en", config.date_on_time_screen);
    prefs.putInt("date_fmt", config.date_format);

    // Timer
    prefs.putBool("tmr_en", config.timer_enabled);
    prefs.putInt("tmr_work", config.timer_work_min);
    prefs.putInt("tmr_brk", config.timer_break_min);
    prefs.putInt("tmr_lbrk", config.timer_long_break_min);
    prefs.putInt("tmr_sess", config.timer_sessions);
    prefs.putBool("tmr_buzz", config.timer_buzzer);

    // Stopwatch
    prefs.putBool("sw_en", config.stopwatch_enabled);

    // Notifications
    prefs.putBool("ntfy_en", config.notify_enabled);
    prefs.putInt("ntfy_dur", config.notify_default_duration);
    prefs.putBool("ntfy_buzz", config.notify_allow_buzzer);

    // System monitor
    prefs.putBool("smon_en", config.sysmon_enabled);
    prefs.putString("smon_lbl", config.sysmon_label);
    prefs.putInt("smon_dmode", config.sysmon_display_mode);
    prefs.putInt("smon_warn", config.sysmon_warn_pct);
    prefs.putInt("smon_crit", config.sysmon_crit_pct);

    // Countdown
    prefs.putBool("cd_en", config.countdown_enabled);
    prefs.putString("cd_name", config.countdown_name);
    prefs.putULong("cd_target", config.countdown_target);

    // Auto-cycle
    prefs.putBool("acyc_en", config.auto_cycle_enabled);
    prefs.putInt("acyc_sec", config.auto_cycle_sec);

    Serial.println("[CONFIG] Saved to NVS");
}

void config_reset() {
    Serial.println("[CONFIG] Factory reset");
    prefs.clear();
    config_set_defaults();
    config_save();
}

AppConfig& config_get() {
    return config;
}

bool config_has_wifi() {
    return strlen(config.wifi_ssid) > 0;
}

bool config_has_server() {
    if (config.data_source == 1) return config_has_dexcom();
    return strlen(config.server_url) > 0;
}

bool config_has_dexcom() {
    return strlen(config.dexcom_username) > 0 && strlen(config.dexcom_password) > 0;
}
