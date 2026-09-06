#include "config_manager.h"
#include "config_transaction.h"
#include <Preferences.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define CONFIG_NAMESPACE "tc001cfg"
#define CONFIG_MAGIC     0x474C5543  // "GLUC"

static AppConfig config;
static Preferences prefs;
static bool config_loaded = false;
static AppConfig committed;
static bool durable=false;
bool config_is_durable() {return durable;}
static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t config_mutex = nullptr;
void config_lock() { if(config_mutex) xSemaphoreTakeRecursive(config_mutex,portMAX_DELAY); }
void config_unlock() { if(config_mutex) xSemaphoreGiveRecursive(config_mutex); }


static void config_set_defaults() {
    memset(&config, 0, sizeof(AppConfig));

    // WiFi (empty by default — triggers AP mode on fresh flash)
    config.wifi_ssid[0] = '\0';
    config.wifi_password[0] = '\0';
    config.wifi_security = 0;      // personal/open
    config.wifi_eap_method = 0;    // PEAP
    config.wifi_identity[0] = '\0';
    config.wifi_eap_password[0] = '\0';
    config.wifi_anon_identity[0] = '\0';
    config.wifi_validate_ca = false;

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
    config.time_display_enabled = true;

    // Default mode
    config.default_mode = 0; // glucose

    // Ambient creature
    config.ambient_enabled = false;
    config.ambient_creature = 0;
    config.ambient_seasonal = true;

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
    config.color_stale       = 0x808080;

    // Clock & weather colors
    config.color_clock   = 0xFFFFFF;
    config.color_weather = 0xFFFFFF;

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
    config.date_on_time_screen = false;
    config.date_format = 0;  // M/DD

    // Pomodoro timer
    config.timer_enabled = false;
    config.timer_work_min = 25;
    config.timer_break_min = 5;
    config.timer_long_break_min = 15;
    config.timer_sessions = 4;
    config.timer_buzzer = true;

    // Stopwatch
    config.stopwatch_enabled = false;

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

    config.auto_update_enabled = true;
    config.auto_update_hour = 3;

    config.magic = CONFIG_MAGIC;
}

static void config_check_littlefs_overlay() {
    if (!LittleFS.begin(false)) {
        Serial.println("[CONFIG] LittleFS mount failed, no overlay to apply");
        return;
    }

    if (!LittleFS.exists("/config.json")) {
        Serial.println("[CONFIG] No /config.json overlay found");
        LittleFS.end();
        return;
    }

    Serial.println("[CONFIG] Found /config.json overlay, applying...");
    File f = LittleFS.open("/config.json", "r");
    if (!f) {
        Serial.println("[CONFIG] Failed to open /config.json");
        LittleFS.end();
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[CONFIG] JSON parse error: %s\n", err.c_str());
        LittleFS.remove("/config.json");
        LittleFS.end();
        return;
    }

    if (doc["wifi_ssid"].is<const char*>())     strncpy(config.wifi_ssid, doc["wifi_ssid"], sizeof(config.wifi_ssid) - 1);
    if (doc["wifi_password"].is<const char*>())  strncpy(config.wifi_password, doc["wifi_password"], sizeof(config.wifi_password) - 1);
    if (doc["wifi_security"].is<int>()) {
        config.wifi_security = doc["wifi_security"];
    } else if (doc["wifi_ssid"].is<const char*>()) {
        // Existing onboarding/Improv-style overlays contain only SSID + PSK.
        config.wifi_security = 0;
        config.wifi_eap_method = 0;
        config.wifi_identity[0] = '\0';
        config.wifi_eap_password[0] = '\0';
        config.wifi_anon_identity[0] = '\0';
        config.wifi_validate_ca = false;
    }
    if (doc["wifi_eap_method"].is<int>())         config.wifi_eap_method = doc["wifi_eap_method"];
    if (doc["wifi_identity"].is<const char*>())   strncpy(config.wifi_identity, doc["wifi_identity"], sizeof(config.wifi_identity) - 1);
    if (doc["wifi_eap_password"].is<const char*>()) strncpy(config.wifi_eap_password, doc["wifi_eap_password"], sizeof(config.wifi_eap_password) - 1);
    if (doc["wifi_anon_identity"].is<const char*>()) strncpy(config.wifi_anon_identity, doc["wifi_anon_identity"], sizeof(config.wifi_anon_identity) - 1);
    config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
    config.wifi_password[sizeof(config.wifi_password) - 1] = '\0';
    config.wifi_identity[sizeof(config.wifi_identity) - 1] = '\0';
    config.wifi_eap_password[sizeof(config.wifi_eap_password) - 1] = '\0';
    config.wifi_anon_identity[sizeof(config.wifi_anon_identity) - 1] = '\0';
    if (doc["data_source"].is<int>())            config.data_source = doc["data_source"];
    if (doc["dexcom_username"].is<const char*>()) strncpy(config.dexcom_username, doc["dexcom_username"], sizeof(config.dexcom_username));
    if (doc["dexcom_password"].is<const char*>()) strncpy(config.dexcom_password, doc["dexcom_password"], sizeof(config.dexcom_password));
    if (doc["dexcom_server"].is<const char*>()) {
        const char* srv = doc["dexcom_server"];
        config.dexcom_us = (strcmp(srv, "US") == 0);
    }
    if (doc["server_url"].is<const char*>())     strncpy(config.server_url, doc["server_url"], sizeof(config.server_url));
    if (doc["auth_token"].is<const char*>())     strncpy(config.auth_token, doc["auth_token"], sizeof(config.auth_token));
    if (doc["timezone"].is<const char*>())       strncpy(config.timezone, doc["timezone"], sizeof(config.timezone));
    if (doc["time_display_enabled"].is<bool>()) config.time_display_enabled = doc["time_display_enabled"];
    if (doc["use_mmol"].is<bool>())              config.use_mmol = doc["use_mmol"];
    if (doc["brightness"].is<int>())             config.brightness = doc["brightness"];
    if (doc["alert_low"].is<int>())              config.alert_low = doc["alert_low"];
    if (doc["alert_high"].is<int>())             config.alert_high = doc["alert_high"];
    config_save();
    Serial.println("[CONFIG] Applied config.json overlay from LittleFS");

    LittleFS.remove("/config.json");
    Serial.println("[CONFIG] Deleted /config.json after applying");
    LittleFS.end();
}

void config_init() {
    config_mutex=xSemaphoreCreateRecursiveMutexStatic(&mutex_storage);
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
        // Absent on configs written before enterprise support: the defaults below
        // reproduce the old personal-WPA2 behaviour exactly.
        config.wifi_security = prefs.getInt("wifi_sec", 0);
        config.wifi_eap_method = prefs.getInt("wifi_eap", 0);
        prefs.getString("wifi_ident", config.wifi_identity, sizeof(config.wifi_identity));
        prefs.getString("wifi_epass", config.wifi_eap_password, sizeof(config.wifi_eap_password));
        prefs.getString("wifi_anon", config.wifi_anon_identity, sizeof(config.wifi_anon_identity));
        config.wifi_validate_ca = prefs.getBool("wifi_ca_val", false);
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
        config.time_display_enabled = prefs.getBool("time_en", true);
        config.default_mode = prefs.getInt("def_mode", 0);

        // Earlier preview builds used mascot-specific keys. Keep those values
        // as one-way fallbacks so test devices retain their saved preferences.
        bool previous_ambient_enabled = prefs.getBool("cat_en", false);
        bool previous_fish_enabled = prefs.getBool("fish_en", previous_ambient_enabled);
        bool previous_ambient_seasonal = prefs.getBool("cat_season", true);
        bool previous_fish_seasonal = prefs.getBool("fish_season", previous_ambient_seasonal);
        config.ambient_enabled = prefs.getBool("amb_en", previous_fish_enabled);
        config.ambient_creature = prefs.getInt("amb_kind", 0);
        if (config.ambient_creature < 0 || config.ambient_creature > 1) {
            config.ambient_creature = 0;
        }
        config.ambient_seasonal = prefs.getBool("amb_season", previous_fish_seasonal);

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
        config.color_stale       = prefs.getUInt("c_stale", 0x808080);

        // Clock & weather colors
        config.color_clock   = prefs.getUInt("c_clock", 0xFFFFFF);
        config.color_weather = prefs.getUInt("c_weather", 0xFFFFFF);

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
        config.date_on_time_screen = prefs.getBool("date_en", false);
        config.date_format = prefs.getInt("date_fmt", 0);

        // Timer
        config.timer_enabled = prefs.getBool("tmr_en", false);
        config.timer_work_min = prefs.getInt("tmr_work", 25);
        config.timer_break_min = prefs.getInt("tmr_brk", 5);
        config.timer_long_break_min = prefs.getInt("tmr_lbrk", 15);
        config.timer_sessions = prefs.getInt("tmr_sess", 4);
        config.timer_buzzer = prefs.getBool("tmr_buzz", true);

        // Stopwatch
        config.stopwatch_enabled = prefs.getBool("sw_en", false);

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

        // OTA settings were added without changing CONFIG_MAGIC, so an existing
        // device receives secure defaults while all other NVS values survive.
        config.auto_update_enabled = prefs.getBool("ota_auto", true);
        config.auto_update_hour = prefs.getInt("ota_hour", 3);
        if (config.auto_update_hour < 0 || config.auto_update_hour > 23) {
            config.auto_update_hour = 3;
        }

        config.magic = CONFIG_MAGIC;

        // Enforce minimum poll interval
        if (config.poll_interval_sec < 15) {
            config.poll_interval_sec = 15;
        }
    }

    // Single NVS blob is the redo journal. It survives interruption while mirroring
    // legacy keys. Unknown keys are never removed. Old firmware ignores this key.
    if(prefs.getBytesLength("pending_v1")==sizeof(AppConfig)) {
        AppConfig recovered;
        if(prefs.getBytes("pending_v1", &recovered,sizeof(recovered))==sizeof(recovered) && recovered.magic==CONFIG_MAGIC) config=recovered;
        config_save();
    }
    committed=config;
    durable=durable || (magic==CONFIG_MAGIC && !prefs.isKey("pending_v1"));
    // Check for config.json overlay from LittleFS (injected by setup app)
    config_check_littlefs_overlay();
    config_loaded = true;

    Serial.printf("[CONFIG] Poll interval: %ds, Brightness: %d\n",
                  config.poll_interval_sec, config.brightness);
}

bool config_save() {
    ConfigGuard guard;
    auto result=config_transaction(
      [] {return prefs.putBytes("pending_v1",&config,sizeof(config))==sizeof(config);},
      [] {
    bool ok=true;
    ok = (prefs.putUInt("magic", CONFIG_MAGIC) > 0) && ok;
    ok = (prefs.putString("wifi_ssid", config.wifi_ssid) == strlen(config.wifi_ssid)) && ok;
    ok = (prefs.getString("wifi_ssid", "__missing__") == config.wifi_ssid) && ok;
    ok = (prefs.putString("wifi_pass", config.wifi_password) == strlen(config.wifi_password)) && ok;
    ok = (prefs.getString("wifi_pass", "__missing__") == config.wifi_password) && ok;
    ok = (prefs.putInt("wifi_sec", config.wifi_security) > 0) && ok;
    ok = (prefs.putInt("wifi_eap", config.wifi_eap_method) > 0) && ok;
    ok = (prefs.putString("wifi_ident", config.wifi_identity) == strlen(config.wifi_identity)) && ok;
    ok = (prefs.getString("wifi_ident", "__missing__") == config.wifi_identity) && ok;
    ok = (prefs.putString("wifi_epass", config.wifi_eap_password) == strlen(config.wifi_eap_password)) && ok;
    ok = (prefs.getString("wifi_epass", "__missing__") == config.wifi_eap_password) && ok;
    ok = (prefs.putString("wifi_anon", config.wifi_anon_identity) == strlen(config.wifi_anon_identity)) && ok;
    ok = (prefs.getString("wifi_anon", "__missing__") == config.wifi_anon_identity) && ok;
    ok = (prefs.putBool("wifi_ca_val", config.wifi_validate_ca) > 0) && ok;
    ok = (prefs.putInt("data_src", config.data_source) > 0) && ok;
    ok = (prefs.putString("server_url", config.server_url) == strlen(config.server_url)) && ok;
    ok = (prefs.getString("server_url", "__missing__") == config.server_url) && ok;
    ok = (prefs.putString("auth_token", config.auth_token) == strlen(config.auth_token)) && ok;
    ok = (prefs.getString("auth_token", "__missing__") == config.auth_token) && ok;
    ok = (prefs.putString("dex_user", config.dexcom_username) == strlen(config.dexcom_username)) && ok;
    ok = (prefs.getString("dex_user", "__missing__") == config.dexcom_username) && ok;
    ok = (prefs.putString("dex_pass", config.dexcom_password) == strlen(config.dexcom_password)) && ok;
    ok = (prefs.getString("dex_pass", "__missing__") == config.dexcom_password) && ok;
    ok = (prefs.putBool("dex_us", config.dexcom_us) > 0) && ok;
    ok = (prefs.putInt("poll_int", config.poll_interval_sec) > 0) && ok;
    ok = (prefs.putUChar("brightness", config.brightness) > 0) && ok;
    ok = (prefs.putBool("auto_brt", config.auto_brightness) > 0) && ok;
    ok = (prefs.putBool("show_delta", config.show_delta) > 0) && ok;
    ok = (prefs.putBool("use_mmol", config.use_mmol) > 0) && ok;
    ok = (prefs.putInt("t_ulow", config.thresh_urgent_low) > 0) && ok;
    ok = (prefs.putInt("t_low", config.thresh_low) > 0) && ok;
    ok = (prefs.putInt("t_high", config.thresh_high) > 0) && ok;
    ok = (prefs.putInt("t_uhigh", config.thresh_urgent_high) > 0) && ok;
    ok = (prefs.putString("timezone", config.timezone) == strlen(config.timezone)) && ok;
    ok = (prefs.getString("timezone", "__missing__") == config.timezone) && ok;
    ok = (prefs.putBool("use_24h", config.use_24h) > 0) && ok;
    ok = (prefs.putBool("time_en", config.time_display_enabled) > 0) && ok;
    ok = (prefs.putInt("def_mode", config.default_mode) > 0) && ok;
    ok = (prefs.putBool("amb_en", config.ambient_enabled) > 0) && ok;
    ok = (prefs.putInt("amb_kind", config.ambient_creature) > 0) && ok;
    ok = (prefs.putBool("amb_season", config.ambient_seasonal) > 0) && ok;

    // Alerts
    ok = (prefs.putBool("alert_en", config.alert_enabled) > 0) && ok;
    ok = (prefs.putInt("alert_low", config.alert_low) > 0) && ok;
    ok = (prefs.putInt("alert_high", config.alert_high) > 0) && ok;
    ok = (prefs.putInt("alert_snz", config.alert_snooze_min) > 0) && ok;

    // Theme colors
    ok = (prefs.putUInt("c_ulow", config.color_urgent_low) > 0) && ok;
    ok = (prefs.putUInt("c_low", config.color_low) > 0) && ok;
    ok = (prefs.putUInt("c_inrange", config.color_in_range) > 0) && ok;
    ok = (prefs.putUInt("c_high", config.color_high) > 0) && ok;
    ok = (prefs.putUInt("c_uhigh", config.color_urgent_high) > 0) && ok;
    ok = (prefs.putUInt("c_stale", config.color_stale) > 0) && ok;

    // Clock & weather colors
    ok = (prefs.putUInt("c_clock", config.color_clock) > 0) && ok;
    ok = (prefs.putUInt("c_weather", config.color_weather) > 0) && ok;

    // Night mode
    ok = (prefs.putBool("night_en", config.night_mode_enabled) > 0) && ok;
    ok = (prefs.putInt("night_start", config.night_start_hour) > 0) && ok;
    ok = (prefs.putInt("night_end", config.night_end_hour) > 0) && ok;
    ok = (prefs.putUChar("night_brt", config.night_brightness) > 0) && ok;

    // Stale timeout
    ok = (prefs.putInt("stale_min", config.stale_timeout_min) > 0) && ok;

    // Weather
    ok = (prefs.putBool("wx_en", config.weather_enabled) > 0) && ok;
    ok = (prefs.putString("wx_apikey", config.weather_api_key) == strlen(config.weather_api_key)) && ok;
    ok = (prefs.getString("wx_apikey", "__missing__") == config.weather_api_key) && ok;
    ok = (prefs.putString("wx_city", config.weather_city) == strlen(config.weather_city)) && ok;
    ok = (prefs.getString("wx_city", "__missing__") == config.weather_city) && ok;
    ok = (prefs.putBool("wx_use_f", config.weather_use_f) > 0) && ok;
    ok = (prefs.putInt("wx_poll", config.weather_poll_min) > 0) && ok;

    // Date display
    ok = (prefs.putBool("date_en", config.date_on_time_screen) > 0) && ok;
    ok = (prefs.putInt("date_fmt", config.date_format) > 0) && ok;

    // Timer
    ok = (prefs.putBool("tmr_en", config.timer_enabled) > 0) && ok;
    ok = (prefs.putInt("tmr_work", config.timer_work_min) > 0) && ok;
    ok = (prefs.putInt("tmr_brk", config.timer_break_min) > 0) && ok;
    ok = (prefs.putInt("tmr_lbrk", config.timer_long_break_min) > 0) && ok;
    ok = (prefs.putInt("tmr_sess", config.timer_sessions) > 0) && ok;
    ok = (prefs.putBool("tmr_buzz", config.timer_buzzer) > 0) && ok;

    // Stopwatch
    ok = (prefs.putBool("sw_en", config.stopwatch_enabled) > 0) && ok;

    // Notifications
    ok = (prefs.putBool("ntfy_en", config.notify_enabled) > 0) && ok;
    ok = (prefs.putInt("ntfy_dur", config.notify_default_duration) > 0) && ok;
    ok = (prefs.putBool("ntfy_buzz", config.notify_allow_buzzer) > 0) && ok;

    // System monitor
    ok = (prefs.putBool("smon_en", config.sysmon_enabled) > 0) && ok;
    ok = (prefs.putString("smon_lbl", config.sysmon_label) == strlen(config.sysmon_label)) && ok;
    ok = (prefs.getString("smon_lbl", "__missing__") == config.sysmon_label) && ok;
    ok = (prefs.putInt("smon_dmode", config.sysmon_display_mode) > 0) && ok;
    ok = (prefs.putInt("smon_warn", config.sysmon_warn_pct) > 0) && ok;
    ok = (prefs.putInt("smon_crit", config.sysmon_crit_pct) > 0) && ok;

    // Countdown
    ok = (prefs.putBool("cd_en", config.countdown_enabled) > 0) && ok;
    ok = (prefs.putString("cd_name", config.countdown_name) == strlen(config.countdown_name)) && ok;
    ok = (prefs.getString("cd_name", "__missing__") == config.countdown_name) && ok;
    ok = (prefs.putULong("cd_target", config.countdown_target) > 0) && ok;

    // Auto-cycle
    ok = (prefs.putBool("acyc_en", config.auto_cycle_enabled) > 0) && ok;
    ok = (prefs.putInt("acyc_sec", config.auto_cycle_sec) > 0) && ok;
    ok = (prefs.putBool("ota_auto", config.auto_update_enabled) > 0) && ok;
    ok = (prefs.putInt("ota_hour", config.auto_update_hour) > 0) && ok;

    // Journal is retained on failure for recovery; do not report a saved value.
    return ok;
      }, [] {return prefs.remove("pending_v1");});
    durable=result==ConfigCommit::Saved;
    if(!durable) {
        if(result==ConfigCommit::Rejected && committed.magic==CONFIG_MAGIC) config=committed;
        return false;
    }
    committed=config;
    Serial.println("[CONFIG] Saved to NVS");
    return true;
}

bool config_bond_reset_pending() {ConfigGuard guard;return prefs.getBool("ble_reset",false);}
void config_bond_reset_finished() {ConfigGuard guard;prefs.remove("ble_reset");}

void config_reset() {
    ConfigGuard guard;
    Serial.println("[CONFIG] Factory reset");
    prefs.clear();
    prefs.putBool("ble_reset",true); // Consumed by BLE on the post-reset boot.
    config_set_defaults();
    config_save();
}

AppConfig config_snapshot() {ConfigGuard guard;return config;}

AppConfig& config_get() {
    return config;
}

bool config_has_wifi() {
    return strlen(config.wifi_ssid) > 0;
}

bool config_has_server() {
    if (config.data_source == 2) return true;  // demo mode needs no config
    if (config.data_source == 1) return config_has_dexcom();
    return strlen(config.server_url) > 0;
}

bool config_has_dexcom() {
    return strlen(config.dexcom_username) > 0 && strlen(config.dexcom_password) > 0;
}

bool config_has_enterprise() {
    return config.wifi_security == 1;
}

bool config_is_loaded() {
    return config_loaded;
}

// --- CA certificate storage (LittleFS) ---

#define CA_PATH "/wifi_ca.pem"

// config_check_littlefs_overlay() unmounts LittleFS when it is done, and
// webserver_init() does not run until well after wifi_init(), so every CA
// accessor mounts on demand and leaves the mount in place.
static bool ca_mount() {
    return LittleFS.begin(false);
}

bool config_ca_exists() {
    if (!ca_mount()) return false;
    return LittleFS.exists(CA_PATH);
}

size_t config_ca_size() {
    if (!ca_mount()) return 0;
    File f = LittleFS.open(CA_PATH, "r");
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
}

size_t config_ca_read(char* out, size_t out_size) {
    if (!out || out_size == 0) return 0;
    out[0] = '\0';
    if (!ca_mount()) return 0;
    File f = LittleFS.open(CA_PATH, "r");
    if (!f) return 0;
    size_t n = f.readBytes(out, out_size - 1);
    out[n] = '\0';
    f.close();
    return n;
}

bool config_ca_write(const char* pem, size_t len) {
    ConfigGuard guard;
    if (!pem || len == 0) return false;
    if (!ca_mount()) return false;
    File f = LittleFS.open(CA_PATH, "w");
    if (!f) return false;
    size_t written = f.write((const uint8_t*)pem, len);
    f.close();
    return written == len;
}

bool config_ca_delete() {
    ConfigGuard guard;
    if (!ca_mount()) return false;
    if (!LittleFS.exists(CA_PATH)) return true;
    return LittleFS.remove(CA_PATH);
}
