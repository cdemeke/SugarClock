#include "config_manager.h"
#include "companion.h"
#include "cgm_accounts.h"
#include <Preferences.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#define CONFIG_NAMESPACE "tc001cfg"
#define CONFIG_MAGIC     0x474C5543  // "GLUC"

static AppConfig config;
static Preferences prefs;
static bool config_loaded = false;

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

    // Glucose accounts: the first slot defaults to a custom URL, others are off.
    config.demo_mode = false;
    config.cgm[0] = cgm_empty_account(CGM_CUSTOM);
    for (int i = 1; i < CGM_MAX_ACCOUNTS; ++i) config.cgm[i] = cgm_empty_account();

    config.poll_interval_sec = 60;

    // Display
    config.brightness = 40;
    config.auto_brightness = true;
    config.glucose_only_when_low = false;
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

    // Pixel companion
    config.ambient_enabled = false;
    config.ambient_character = COMPANION_FISH;
    config.ambient_style = COMPANION_TEXT;
    config.ambient_use_glucose_colors = false;
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
    config.color_stale       = 0x390E3B;

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

static void copy_text(char* out, size_t size, const char* text) {
    strncpy(out, text ? text : "", size - 1);
    out[size - 1] = '\0';
}

// The setup app writes the legacy single-source keys for the first account.
static void apply_overlay_account(JsonDocument& doc) {
    if (!doc["data_source"].is<int>()) return;
    const int source = doc["data_source"];
    config.demo_mode = source == CGM_DEMO_SOURCE;
    if (config.demo_mode || !cgm_provider_valid(source)) return;
    CgmAccount& a = config.cgm[0];
    const CgmAccount saved = a;
    a = cgm_empty_account(source);
    strcpy(a.label, saved.label);
    if (source == CGM_DEXCOM) {
        copy_text(a.username, sizeof(a.username), doc["dexcom_username"] | "");
        copy_text(a.password, sizeof(a.password), doc["dexcom_password"] | "");
        a.us = strcmp(doc["dexcom_server"] | "US", "US") == 0;
    } else if (source == CGM_LIBRE) {
        copy_text(a.username, sizeof(a.username), doc["libre_email"] | "");
        copy_text(a.password, sizeof(a.password), doc["libre_password"] | "");
        if (saved.provider == CGM_LIBRE && strcmp(a.username, saved.username) == 0) {
            strcpy(a.region, saved.region);
            strcpy(a.patient_id, saved.patient_id);
            strcpy(a.patient_name, saved.patient_name);
        }
    } else {
        copy_text(a.url, sizeof(a.url), doc["server_url"] | "");
        copy_text(a.token, sizeof(a.token), doc["auth_token"] | "");
    }
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
    apply_overlay_account(doc);
    if (doc["timezone"].is<const char*>())       strncpy(config.timezone, doc["timezone"], sizeof(config.timezone));
    if (doc["time_display_enabled"].is<bool>()) config.time_display_enabled = doc["time_display_enabled"];
    if (doc["glucose_only_when_low"].is<bool>()) config.glucose_only_when_low = doc["glucose_only_when_low"];
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

// The first account keeps the pre-multi-account keys so a rollback to older
// firmware still finds it. Unused providers' keys are cleared on save.
static int infer_legacy_provider() {
    char text[8] = "";
    if (prefs.getString("dex_user", text, sizeof(text)) && text[0]) return CGM_DEXCOM;
    if (prefs.getString("llu_email", text, sizeof(text)) && text[0]) return CGM_LIBRE;
    return CGM_CUSTOM;
}

static void load_accounts() {
    const int source = prefs.getInt("data_src", CGM_CUSTOM);
    config.demo_mode = source == CGM_DEMO_SOURCE;
    int provider = config.demo_mode
        ? (prefs.isKey("cgm0_type") ? prefs.getInt("cgm0_type", CGM_CUSTOM) : infer_legacy_provider())
        : source;
    if (!cgm_provider_valid(provider)) provider = CGM_CUSTOM;
    CgmAccount& first = config.cgm[0];
    first = cgm_empty_account(provider);
    prefs.getString("dex_label", first.label, sizeof(first.label));
    if (provider == CGM_DEXCOM) {
        prefs.getString("dex_user", first.username, sizeof(first.username));
        prefs.getString("dex_pass", first.password, sizeof(first.password));
        first.us = prefs.getBool("dex_us", true);
    } else if (provider == CGM_LIBRE) {
        prefs.getString("llu_email", first.username, sizeof(first.username));
        prefs.getString("llu_pass", first.password, sizeof(first.password));
        prefs.getString("llu_region", first.region, sizeof(first.region));
        prefs.getString("llu_patient", first.patient_id, sizeof(first.patient_id));
        prefs.getString("llu_name", first.patient_name, sizeof(first.patient_name));
    } else {
        prefs.getString("server_url", first.url, sizeof(first.url));
        prefs.getString("auth_token", first.token, sizeof(first.token));
    }

    for (int i = 1; i < CGM_MAX_ACCOUNTS; ++i) {
        char key[16];
        CgmAccount& a = config.cgm[i];
        snprintf(key, sizeof(key), "cgm%d_type", i);
        if (!prefs.isKey(key)) {
            // Preview builds stored extra Dexcom accounts under dexN_* keys.
            a = cgm_empty_account();
            snprintf(key, sizeof(key), "dex%d_user", i);
            if (!prefs.isKey(key)) continue;
            a.provider = CGM_DEXCOM;
            prefs.getString(key, a.username, sizeof(a.username));
            snprintf(key, sizeof(key), "dex%d_pass", i);
            prefs.getString(key, a.password, sizeof(a.password));
            snprintf(key, sizeof(key), "dex%d_label", i);
            prefs.getString(key, a.label, sizeof(a.label));
            snprintf(key, sizeof(key), "dex%d_us", i);
            a.us = prefs.getBool(key, true);
            if (!a.username[0]) a = cgm_empty_account();
            continue;
        }
        a = cgm_empty_account(prefs.getInt(key, CGM_NONE));
        if (!cgm_provider_valid(a.provider)) { a = cgm_empty_account(); continue; }
        snprintf(key, sizeof(key), "cgm%d_lbl", i);  prefs.getString(key, a.label, sizeof(a.label));
        snprintf(key, sizeof(key), "cgm%d_user", i); prefs.getString(key, a.username, sizeof(a.username));
        snprintf(key, sizeof(key), "cgm%d_pass", i); prefs.getString(key, a.password, sizeof(a.password));
        snprintf(key, sizeof(key), "cgm%d_us", i);   a.us = prefs.getBool(key, true);
        snprintf(key, sizeof(key), "cgm%d_url", i);  prefs.getString(key, a.url, sizeof(a.url));
        snprintf(key, sizeof(key), "cgm%d_tok", i);  prefs.getString(key, a.token, sizeof(a.token));
        snprintf(key, sizeof(key), "cgm%d_reg", i);  prefs.getString(key, a.region, sizeof(a.region));
        snprintf(key, sizeof(key), "cgm%d_pid", i);  prefs.getString(key, a.patient_id, sizeof(a.patient_id));
        snprintf(key, sizeof(key), "cgm%d_pnm", i);  prefs.getString(key, a.patient_name, sizeof(a.patient_name));
    }
}

static void save_accounts() {
    const CgmAccount& first = config.cgm[0];
    const bool dexcom = first.provider == CGM_DEXCOM;
    const bool libre = first.provider == CGM_LIBRE;
    const bool url = cgm_uses_url(first.provider);
    prefs.putInt("data_src", config_data_source());
    prefs.putInt("cgm0_type", first.provider);
    prefs.putString("dex_label", first.label);
    prefs.putString("dex_user", dexcom ? first.username : "");
    prefs.putString("dex_pass", dexcom ? first.password : "");
    prefs.putBool("dex_us", dexcom ? first.us : true);
    prefs.putString("llu_email", libre ? first.username : "");
    prefs.putString("llu_pass", libre ? first.password : "");
    prefs.putString("llu_region", libre ? first.region : "");
    prefs.putString("llu_patient", libre ? first.patient_id : "");
    prefs.putString("llu_name", libre ? first.patient_name : "");
    prefs.putString("server_url", url ? first.url : "");
    prefs.putString("auth_token", url ? first.token : "");

    for (int i = 1; i < CGM_MAX_ACCOUNTS; ++i) {
        char key[16];
        const CgmAccount& a = config.cgm[i];
        for (const char* old : {"user", "pass", "label", "us"}) {
            snprintf(key, sizeof(key), "dex%d_%s", i, old);
            if (prefs.isKey(key)) prefs.remove(key);
        }
        snprintf(key, sizeof(key), "cgm%d_type", i); prefs.putInt(key, a.provider);
        snprintf(key, sizeof(key), "cgm%d_lbl", i);  prefs.putString(key, a.label);
        snprintf(key, sizeof(key), "cgm%d_user", i); prefs.putString(key, a.username);
        snprintf(key, sizeof(key), "cgm%d_pass", i); prefs.putString(key, a.password);
        snprintf(key, sizeof(key), "cgm%d_us", i);   prefs.putBool(key, a.us);
        snprintf(key, sizeof(key), "cgm%d_url", i);  prefs.putString(key, a.url);
        snprintf(key, sizeof(key), "cgm%d_tok", i);  prefs.putString(key, a.token);
        snprintf(key, sizeof(key), "cgm%d_reg", i);  prefs.putString(key, a.region);
        snprintf(key, sizeof(key), "cgm%d_pid", i);  prefs.putString(key, a.patient_id);
        snprintf(key, sizeof(key), "cgm%d_pnm", i);  prefs.putString(key, a.patient_name);
    }
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
        // Absent on configs written before enterprise support: the defaults below
        // reproduce the old personal-WPA2 behaviour exactly.
        config.wifi_security = prefs.getInt("wifi_sec", 0);
        config.wifi_eap_method = prefs.getInt("wifi_eap", 0);
        prefs.getString("wifi_ident", config.wifi_identity, sizeof(config.wifi_identity));
        prefs.getString("wifi_epass", config.wifi_eap_password, sizeof(config.wifi_eap_password));
        prefs.getString("wifi_anon", config.wifi_anon_identity, sizeof(config.wifi_anon_identity));
        config.wifi_validate_ca = prefs.getBool("wifi_ca_val", false);
        load_accounts();
        config.poll_interval_sec = prefs.getInt("poll_int", 60);
        config.brightness = prefs.getUChar("brightness", 40);
        config.auto_brightness = prefs.getBool("auto_brt", true);
        config.glucose_only_when_low = prefs.getBool("low_only", false);
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

        // The first preview build used mascot-specific keys. Keep those values
        // as one-way fallbacks so test devices retain their saved preferences.
        bool previous_ambient_enabled = prefs.getBool("cat_en", false);
        bool previous_ambient_seasonal = prefs.getBool("cat_season", true);
        config.ambient_enabled = prefs.getBool("amb_en", prefs.getBool("fish_en", previous_ambient_enabled));
        config.ambient_seasonal = prefs.getBool("amb_season", prefs.getBool("fish_season", previous_ambient_seasonal));
        config.ambient_character = companion_or_default(prefs.getInt("pal_type", prefs.getInt("amb_kind", COMPANION_FISH)));
        config.ambient_use_glucose_colors = prefs.getBool("pal_colors", false);
        config.ambient_style = companion_style_or_default(prefs.getInt("pal_style", COMPANION_TEXT));

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
        config.color_stale       = prefs.getUInt("c_stale", 0x390E3B);

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

    // Check for config.json overlay from LittleFS (injected by setup app)
    config_check_littlefs_overlay();
    config_loaded = true;

    Serial.printf("[CONFIG] Poll interval: %ds, Brightness: %d\n",
                  config.poll_interval_sec, config.brightness);
}

void config_save() {
    LibreConfigLock lock(config_libre_mutex());
    prefs.putUInt("magic", CONFIG_MAGIC);
    prefs.putString("wifi_ssid", config.wifi_ssid);
    prefs.putString("wifi_pass", config.wifi_password);
    prefs.putInt("wifi_sec", config.wifi_security);
    prefs.putInt("wifi_eap", config.wifi_eap_method);
    prefs.putString("wifi_ident", config.wifi_identity);
    prefs.putString("wifi_epass", config.wifi_eap_password);
    prefs.putString("wifi_anon", config.wifi_anon_identity);
    prefs.putBool("wifi_ca_val", config.wifi_validate_ca);
    save_accounts();
    prefs.putInt("poll_int", config.poll_interval_sec);
    prefs.putUChar("brightness", config.brightness);
    prefs.putBool("auto_brt", config.auto_brightness);
    prefs.putBool("low_only", config.glucose_only_when_low);
    prefs.putBool("show_delta", config.show_delta);
    prefs.putBool("use_mmol", config.use_mmol);
    prefs.putInt("t_ulow", config.thresh_urgent_low);
    prefs.putInt("t_low", config.thresh_low);
    prefs.putInt("t_high", config.thresh_high);
    prefs.putInt("t_uhigh", config.thresh_urgent_high);
    prefs.putString("timezone", config.timezone);
    prefs.putBool("use_24h", config.use_24h);
    prefs.putBool("time_en", config.time_display_enabled);
    prefs.putInt("def_mode", config.default_mode);
    prefs.putBool("amb_en", config.ambient_enabled);
    config.ambient_character = companion_or_default(config.ambient_character);
    prefs.putInt("pal_type", config.ambient_character);
    config.ambient_style = companion_style_or_default(config.ambient_style);
    prefs.putInt("pal_style", config.ambient_style);
    prefs.putBool("pal_colors", config.ambient_use_glucose_colors);
    prefs.putBool("amb_season", config.ambient_seasonal);

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
    prefs.putUInt("c_stale", config.color_stale);

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
    prefs.putBool("ota_auto", config.auto_update_enabled);
    prefs.putInt("ota_hour", config.auto_update_hour);

    Serial.println("[CONFIG] Saved to NVS");
}

void config_reset() {
    LibreConfigLock lock(config_libre_mutex());
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
    LibreConfigLock lock(config_libre_mutex());
    return config.demo_mode || cgm_account_configured(config.cgm[0]);
}

int config_data_source() {
    return config.demo_mode ? CGM_DEMO_SOURCE : config.cgm[0].provider;
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
    if (!pem || len == 0) return false;
    if (!ca_mount()) return false;
    File f = LittleFS.open(CA_PATH, "w");
    if (!f) return false;
    size_t written = f.write((const uint8_t*)pem, len);
    f.close();
    return written == len;
}

bool config_ca_delete() {
    if (!ca_mount()) return false;
    if (!LittleFS.exists(CA_PATH)) return true;
    return LittleFS.remove(CA_PATH);
}
