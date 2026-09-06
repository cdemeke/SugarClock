#include "web_server.h"
#include "config_manager.h"
#include "config_patch.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "glucose_engine.h"
#include "time_engine.h"
#include "sensors.h"
#include "display.h"
#include "weather_client.h"
#include "timer_engine.h"
#include "notify_engine.h"
#include "sysmon_engine.h"
#include "countdown_engine.h"
#include "buzzer.h"
#include "buttons.h"
#include "hardware_pins.h"
#include "captive_portal.h"
#include "net_check.h"
#include "web_assets.h"
#include "ota_manager.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>

static AsyncWebServer server(80);
static bool started = false;

#ifndef SUGARCLOCK_VERSION
#define SUGARCLOCK_VERSION "unknown"
#endif
#ifndef SUGARCLOCK_HARDWARE_ID
#define SUGARCLOCK_HARDWARE_ID "ulanzi-tc001-esp32-4mb"
#endif

// Helper: convert uint32_t RGB to "#RRGGBB" hex string
static String color_to_hex(uint32_t c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    return String(buf);
}

// Helper: parse "#RRGGBB" hex string to uint32_t RGB
static uint32_t hex_to_color(const char* hex) {
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return 0;
    return strtoul(hex + 1, NULL, 16);
}

// GET /api/status
static void handle_status(AsyncWebServerRequest* request) {
    JsonDocument doc;

    const GlucoseReading& r = http_get_reading();
    doc["glucose"] = r.valid ? r.glucose : 0;
    doc["trend"] = r.valid ? TREND_NAMES[r.trend] : "Unknown";
    doc["valid"] = r.valid;
    doc["data_age_sec"] = r.valid ? (millis() - r.received_at_ms) / 1000 : -1;
    doc["state"] = engine_state_name(engine_get_state());
    doc["wifi_connected"] = wifi_is_connected();
    doc["wifi_ip"] = wifi_get_ip();
    doc["wifi_rssi"] = wifi_get_rssi();
    doc["uptime_sec"] = time_get_uptime_sec();
    doc["failure_count"] = http_get_failure_count();
    doc["brightness"] = display_get_brightness();
    doc["message"] = r.message;
    OtaStatusSnapshot ota;
    ota_get_status(ota);
    doc["firmware_version"] = SUGARCLOCK_VERSION;
    doc["hardware"] = SUGARCLOCK_HARDWARE_ID;
    doc["running_partition"] = ota.running_partition;
    doc["boot_partition"] = ota.boot_partition;

    // Delta
    doc["delta"] = http_get_delta();

    // Glucose color info
    ConfigGuard guard;
    AppConfig& cfg = config_get();
    unsigned long age = http_time_since_last_reading();
    unsigned long stale_ms = (unsigned long)cfg.stale_timeout_min * 60UL * 1000UL;
    int failures = http_get_failure_count();
    bool is_stale = (cfg.data_source != 2) && (age >= stale_ms || failures >= 5);

    if (r.valid) {
        if (is_stale) {
            doc["color"] = "gray";
        } else if (r.glucose < cfg.thresh_urgent_low || r.glucose > cfg.thresh_urgent_high) {
            doc["color"] = "red";
        } else if (r.glucose < cfg.thresh_low || r.glucose > cfg.thresh_high) {
            doc["color"] = "orange";
        } else {
            doc["color"] = "green";
        }
    } else {
        doc["color"] = "gray";
    }

    // Thresholds (for web UI graph)
    JsonObject thresholds = doc["thresholds"].to<JsonObject>();
    thresholds["urgent_low"] = cfg.thresh_urgent_low;
    thresholds["low"] = cfg.thresh_low;
    thresholds["high"] = cfg.thresh_high;
    thresholds["urgent_high"] = cfg.thresh_urgent_high;

    // Weather
    if (weather_has_data()) {
        const WeatherReading& wx = weather_get_reading();
        doc["weather_temp"] = wx.temp;
        doc["weather_desc"] = wx.description;
        doc["weather_humidity"] = wx.humidity;
    }

    // Timer status
    doc["timer_state"] = (int)timer_get_state();
    doc["timer_remaining"] = timer_get_remaining_sec();
    doc["timer_session"] = timer_get_session();

    // Stopwatch status
    doc["stopwatch_state"] = (int)stopwatch_get_state();
    doc["stopwatch_elapsed"] = stopwatch_get_elapsed_sec();

    // Sysmon
    if (sysmon_has_data()) {
        doc["sysmon_label"] = sysmon_get_label();
        doc["sysmon_value"] = sysmon_get_value();
        doc["sysmon_max"] = sysmon_get_max();
    }

    // Countdown
    if (cfg.countdown_enabled && countdown_is_configured()) {
        doc["countdown_remaining"] = countdown_get_remaining_sec();
        doc["countdown_name"] = cfg.countdown_name;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/config
static void handle_get_config(AsyncWebServerRequest* request) {
    ConfigGuard guard;
    AppConfig& cfg = config_get();
    JsonDocument doc;

    // Secrets are never echoed back. This endpoint is reachable over plain HTTP,
    // including from an open setup AP, so the UI is told only whether a value is
    // set. POST treats an empty string as "leave unchanged", which is what makes
    // a round-trip through the form safe.
    doc["wifi_ssid"] = cfg.wifi_ssid;
    doc["has_wifi_password"] = strlen(cfg.wifi_password) > 0;
    doc["wifi_security"] = cfg.wifi_security;
    doc["wifi_eap_method"] = cfg.wifi_eap_method;
    doc["wifi_identity"] = cfg.wifi_identity;
    doc["has_wifi_eap_password"] = strlen(cfg.wifi_eap_password) > 0;
    doc["wifi_anon_identity"] = cfg.wifi_anon_identity;
    doc["wifi_validate_ca"] = cfg.wifi_validate_ca;
    doc["has_wifi_ca"] = config_ca_exists();
    doc["data_source"] = cfg.data_source;
    doc["has_server_url"] = strlen(cfg.server_url) > 0;
    doc["has_auth_token"] = strlen(cfg.auth_token) > 0;
    doc["dexcom_username"] = cfg.dexcom_username;
    doc["has_dexcom_password"] = strlen(cfg.dexcom_password) > 0;
    doc["dexcom_us"] = cfg.dexcom_us;
    doc["poll_interval"] = cfg.poll_interval_sec;
    doc["brightness"] = cfg.brightness;
    doc["auto_brightness"] = cfg.auto_brightness;
    doc["show_delta"] = cfg.show_delta;
    doc["use_mmol"] = cfg.use_mmol;
    doc["thresh_urgent_low"] = cfg.thresh_urgent_low;
    doc["thresh_low"] = cfg.thresh_low;
    doc["thresh_high"] = cfg.thresh_high;
    doc["thresh_urgent_high"] = cfg.thresh_urgent_high;
    doc["timezone"] = cfg.timezone;
    doc["use_24h"] = cfg.use_24h;
    doc["time_display_enabled"] = cfg.time_display_enabled;
    doc["default_mode"] = cfg.default_mode;
    doc["ambient_enabled"] = cfg.ambient_enabled;
    doc["ambient_creature"] = cfg.ambient_creature;
    doc["ambient_seasonal"] = cfg.ambient_seasonal;

    // Alerts
    doc["alert_enabled"] = cfg.alert_enabled;
    doc["alert_low"] = cfg.alert_low;
    doc["alert_high"] = cfg.alert_high;
    doc["alert_snooze_min"] = cfg.alert_snooze_min;

    // Theme colors
    doc["color_urgent_low"] = color_to_hex(cfg.color_urgent_low);
    doc["color_low"] = color_to_hex(cfg.color_low);
    doc["color_in_range"] = color_to_hex(cfg.color_in_range);
    doc["color_high"] = color_to_hex(cfg.color_high);
    doc["color_urgent_high"] = color_to_hex(cfg.color_urgent_high);
    doc["color_stale"] = color_to_hex(cfg.color_stale);

    // Clock & weather colors
    doc["color_clock"] = color_to_hex(cfg.color_clock);
    doc["color_weather"] = color_to_hex(cfg.color_weather);

    // Night mode
    doc["night_mode_enabled"] = cfg.night_mode_enabled;
    doc["night_start_hour"] = cfg.night_start_hour;
    doc["night_end_hour"] = cfg.night_end_hour;
    doc["night_brightness"] = cfg.night_brightness;

    // Stale timeout
    doc["stale_timeout_min"] = cfg.stale_timeout_min;

    // Weather
    doc["weather_enabled"] = cfg.weather_enabled;
    doc["has_weather_api_key"] = strlen(cfg.weather_api_key) > 0;
    doc["weather_city"] = cfg.weather_city;
    doc["weather_use_f"] = cfg.weather_use_f;
    doc["weather_poll_min"] = cfg.weather_poll_min;

    // Date
    doc["date_on_time_screen"] = cfg.date_on_time_screen;
    doc["date_format"] = cfg.date_format;

    // Timer
    doc["timer_enabled"] = cfg.timer_enabled;
    doc["timer_work_min"] = cfg.timer_work_min;
    doc["timer_break_min"] = cfg.timer_break_min;
    doc["timer_long_break_min"] = cfg.timer_long_break_min;
    doc["timer_sessions"] = cfg.timer_sessions;
    doc["timer_buzzer"] = cfg.timer_buzzer;

    // Stopwatch
    doc["stopwatch_enabled"] = cfg.stopwatch_enabled;

    // Notifications
    doc["notify_enabled"] = cfg.notify_enabled;
    doc["notify_default_duration"] = cfg.notify_default_duration;
    doc["notify_allow_buzzer"] = cfg.notify_allow_buzzer;

    // Sysmon
    doc["sysmon_enabled"] = cfg.sysmon_enabled;
    doc["sysmon_label"] = cfg.sysmon_label;
    doc["sysmon_display_mode"] = cfg.sysmon_display_mode;
    doc["sysmon_warn_pct"] = cfg.sysmon_warn_pct;
    doc["sysmon_crit_pct"] = cfg.sysmon_crit_pct;

    // Countdown
    doc["countdown_enabled"] = cfg.countdown_enabled;
    doc["countdown_name"] = cfg.countdown_name;
    doc["countdown_target"] = cfg.countdown_target;

    // Auto-cycle
    doc["auto_cycle_enabled"] = cfg.auto_cycle_enabled;
    doc["auto_cycle_sec"] = cfg.auto_cycle_sec;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/config (JSON body) — accumulate chunks before parsing
static void handle_post_config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if(total>4095 || index>total || len>total-index) {request->send(413,"application/json","{\"error\":\"Body too large\"}");return;}
    // AsyncWebServer frees _tempObject on aborted requests. Each request owns its
    // buffer so simultaneous web posts cannot combine fragments or credentials.
    if(index==0) {request->_tempObject=calloc(total+1,1);}
    if(!request->_tempObject) {request->send(503,"application/json","{\"error\":\"busy\"}");return;}
    char* body=static_cast<char*>(request->_tempObject);
    memcpy(body+index,data,len);if(index+len<total) return;
    JsonDocument doc;
    DeserializationError err=deserializeJson(doc,body,total,DeserializationOption::NestingLimit(6));
    memset(body,0,total);free(body);request->_tempObject=nullptr;
    if(err) {request->send(400,"application/json","{\"error\":\"Invalid JSON\"}");return;}
    if(ota_is_busy()) {request->send(409,"application/json","{\"error\":\"ota_busy\"}");return;}
    ConfigGuard guard;
    AppConfig candidate=config_get();
    const char* error=config_patch(candidate,doc.as<JsonObjectConst>(),true);
    if(error) { request->send(400,"application/json",String("{\"error\":\"")+error+"\"}");return; }
    const AppConfig& current=config_get();
    if(strcmp(candidate.wifi_ssid,current.wifi_ssid) || strcmp(candidate.wifi_password,current.wifi_password) ||
       candidate.wifi_security!=current.wifi_security || candidate.wifi_eap_method!=current.wifi_eap_method ||
       strcmp(candidate.wifi_identity,current.wifi_identity) || strcmp(candidate.wifi_eap_password,current.wifi_eap_password) ||
       strcmp(candidate.wifi_anon_identity,current.wifi_anon_identity) || candidate.wifi_validate_ca!=current.wifi_validate_ca) {
        request->send(409,"application/json","{\"error\":\"use_wifi_trial\"}");return;
    }
    bool sourceChanged=config_source_changed(config_get(),candidate);
    config_get()=candidate;
    if(sourceChanged) http_configuration_changed();
    if(!config_save()) { request->send(500,"application/json","{\"error\":\"persistence_failed\"}");return; }
    setenv("TZ",candidate.timezone,1);tzset();
    engine_rebuild_toggle_order();
    if(!candidate.auto_brightness) display_set_brightness(candidate.brightness);

    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// GET /api/debug
static void handle_debug(AsyncWebServerRequest* request) {
    JsonDocument doc;

    doc["last_http_code"] = http_get_last_response_code();
    doc["last_http_body"] = http_get_last_response_body();
    doc["failure_count"] = http_get_failure_count();
    doc["ever_received"] = http_has_ever_received();
    doc["wifi_rssi"] = wifi_get_rssi();
    doc["wifi_status"] = wifi_get_status();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["min_free_heap"] = ESP.getMinFreeHeap();
    doc["largest_free_block"] = ESP.getMaxAllocHeap();
    doc["uptime_sec"] = time_get_uptime_sec();
    doc["display_state"] = engine_state_name(engine_get_state());
    doc["ldr_raw"] = sensors_get_ldr();
    doc["auto_brightness_val"] = sensors_get_auto_brightness();
    doc["battery_voltage"] = sensors_get_battery_voltage();
    doc["battery_percent"] = sensors_get_battery_percent();

    // Button GPIO states (LOW = pressed on active-LOW buttons)
    doc["btn_left_raw"] = digitalRead(PIN_BUTTON_LEFT);
    doc["btn_middle_raw"] = digitalRead(PIN_BUTTON_MIDDLE);
    doc["btn_right_raw"] = digitalRead(PIN_BUTTON_RIGHT);
    doc["user_mode"] = engine_state_name(engine_get_user_mode());

    // MAC address
    doc["mac"] = WiFi.macAddress();

    // LittleFS info
    doc["fs_used"] = LittleFS.usedBytes();
    doc["fs_total"] = LittleFS.totalBytes();

    unsigned long age = http_time_since_last_reading();
    doc["data_age_ms"] = (age == ULONG_MAX) ? -1 : (long)age;

    const GlucoseReading& r = http_get_reading();
    if (r.valid) {
        doc["raw_glucose"] = r.glucose;
        doc["raw_trend"] = TREND_NAMES[r.trend];
        doc["raw_message"] = r.message;
        doc["raw_force_mode"] = r.force_mode;
        doc["raw_delta"] = http_get_delta();
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/history
static void handle_history(AsyncWebServerRequest* request) {
    JsonDocument doc;

    GlucoseHistoryEntry entries[GLUCOSE_HISTORY_SIZE];
    int count = http_get_history(entries, GLUCOSE_HISTORY_SIZE);

    JsonArray readings = doc["readings"].to<JsonArray>();
    for (int i = 0; i < count; i++) {
        JsonObject entry = readings.add<JsonObject>();
        entry["glucose"] = entries[i].glucose;
        entry["delta"] = entries[i].delta;
        entry["ts"] = entries[i].timestamp;
    }
    doc["count"] = count;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/timer
static void handle_timer_status(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["timer_state"] = (int)timer_get_state();
    doc["timer_remaining"] = timer_get_remaining_sec();
    doc["timer_session"] = timer_get_session();
    doc["timer_total_sessions"] = timer_get_total_sessions();
    doc["stopwatch_state"] = (int)stopwatch_get_state();
    doc["stopwatch_elapsed"] = stopwatch_get_elapsed_sec();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/ota/status
static void handle_ota_status(AsyncWebServerRequest* request) {
    OtaStatusSnapshot snapshot;
    ota_get_status(snapshot);
    JsonDocument doc;
    doc["current_version"] = snapshot.current_version;
    doc["state"] = ota_state_name(snapshot.state);
    doc["available_version"] = snapshot.available_version;
    doc["progress"] = snapshot.progress;
    doc["last_check"] = snapshot.last_check;
    doc["last_error"] = snapshot.last_error;
    doc["safety_reason"] = snapshot.safety_reason;
    doc["auto_update_enabled"] = snapshot.auto_update_enabled;
    doc["auto_update_hour"] = snapshot.auto_update_hour;
    doc["running_partition"] = snapshot.running_partition;
    doc["boot_partition"] = snapshot.boot_partition;
    doc["pending_verification"] = snapshot.pending_verification;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

static void send_ota_request_result(AsyncWebServerRequest* request, OtaRequestResult result) {
    switch (result) {
        case OTA_REQUEST_QUEUED:
            request->send(202, "application/json", "{\"status\":\"queued\"}");
            break;
        case OTA_REQUEST_BUSY:
            request->send(409, "application/json", "{\"error\":\"ota_busy\"}");
            break;
        case OTA_REQUEST_UNSAFE: {
            OtaStatusSnapshot snapshot;
            ota_get_status(snapshot);
            JsonDocument doc;
            doc["error"] = "safety_requirements_not_met";
            doc["reason"] = snapshot.safety_reason;
            String output;
            serializeJson(doc, output);
            request->send(412, "application/json", output);
            break;
        }
        case OTA_REQUEST_NO_UPDATE:
            request->send(409, "application/json", "{\"error\":\"no_update_available\"}");
            break;
        default:
            request->send(500, "application/json", "{\"error\":\"internal_failure\"}");
            break;
    }
}

static void handle_ota_settings(AsyncWebServerRequest* request,uint8_t* data,size_t len,size_t index,size_t total) {
    handle_post_config(request,data,len,index,total);
}

// POST /api/notify
static void handle_post_notify(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (index != 0) return;

    ConfigGuard guard;
    AppConfig& cfg = config_get();
    if (!cfg.notify_enabled) {
        request->send(403, "application/json", "{\"error\":\"Notifications disabled\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* text = doc["text"] | "";
    if (strlen(text) == 0) {
        request->send(400, "application/json", "{\"error\":\"Missing text\"}");
        return;
    }

    int duration = doc["duration_sec"] | cfg.notify_default_duration;
    bool urgent = doc["urgent"] | false;

    notify_push(text, duration, urgent);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// POST /api/sysmon
static void handle_post_sysmon(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (index != 0) return;

    ConfigGuard guard;
    AppConfig& cfg = config_get();
    if (!cfg.sysmon_enabled) {
        request->send(403, "application/json", "{\"error\":\"System monitor disabled\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* label = doc["label"] | cfg.sysmon_label;
    int value = doc["value"] | 0;
    int max_val = doc["max"] | 100;

    sysmon_push(label, value, max_val);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// POST /api/test/weather
static void handle_test_weather(AsyncWebServerRequest* request) {
    bool ok = weather_force_fetch();
    JsonDocument doc;
    doc["ok"] = ok;
    doc["http_code"] = weather_get_last_http_code();

    if (ok) {
        const WeatherReading& wx = weather_get_reading();
        doc["temp"] = wx.temp;
        doc["description"] = wx.description;
        doc["humidity"] = wx.humidity;
    } else {
        doc["error"] = weather_get_last_response();
    }

    String output;
    serializeJson(doc, output);
    request->send(ok ? 200 : 502, "application/json", output);
}

// POST /api/test/weather-mock
static void handle_test_weather_mock(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (index != 0) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    float temp = doc["temp"] | 32.0f;
    const char* desc = doc["description"] | "Mock";
    int cid = doc["condition_id"] | 800;

    weather_set_mock(temp, desc, cid);

    // Force weather enabled + switch to weather display
    ConfigGuard guard;
    AppConfig& cfg = config_get();
    cfg.weather_enabled = true;
    engine_rebuild_toggle_order();
    engine_force_state(STATE_WEATHER_DISPLAY);

    JsonDocument resp;
    resp["status"] = "ok";
    resp["condition_id"] = cid;
    resp["description"] = desc;
    resp["temp"] = temp;

    String output;
    serializeJson(resp, output);
    request->send(200, "application/json", output);
}

// POST /api/test/glucose
static void handle_test_glucose(AsyncWebServerRequest* request) {
    AppConfig cfg=config_snapshot();
    if(cfg.data_source!=2 && (!wifi_is_connected() || !config_has_server())) {
        request->send(400,"application/json","{\"ok\":false,\"error\":\"Configure WiFi and a data source first\"}");return;
    }
    unsigned long generation=http_fetch_generation();
    if(!http_force_fetch()) {request->send(409,"application/json","{\"ok\":false,\"error\":\"Network busy\"}");return;}
    JsonDocument doc;doc["queued"]=true;doc["after_generation"]=generation;
    String output;serializeJson(doc,output);request->send(202,"application/json",output);
}
static void handle_test_glucose_result(AsyncWebServerRequest* request) {
    auto reading=http_get_reading();JsonDocument doc;
    doc["generation"]=http_fetch_generation();doc["pending"]=http_is_fetching();
    doc["ok"]=reading.valid && http_get_failure_count()==0;
    doc["glucose"]=reading.glucose;doc["trend"]=TREND_NAMES[reading.trend];
    doc["http_code"]=http_get_last_response_code();
    if(!reading.valid || http_get_failure_count()) doc["error"]="No new valid reading; check provider credentials and network access";
    String output;serializeJson(doc,output);request->send(200,"application/json",output);
}

// POST /api/display/next
static void handle_display_next(AsyncWebServerRequest* request) {
    engine_toggle_mode();
    JsonDocument doc;
    doc["status"] = "ok";
    doc["mode"] = engine_state_name(engine_get_user_mode());
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/display/prev
static void handle_display_prev(AsyncWebServerRequest* request) {
    engine_toggle_mode_prev();
    JsonDocument doc;
    doc["status"] = "ok";
    doc["mode"] = engine_state_name(engine_get_user_mode());
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/restart
static void handle_restart(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
}

// POST /api/factory-reset
static void handle_factory_reset(AsyncWebServerRequest* request) {
    config_reset();
    request->send(200, "application/json", "{\"status\":\"factory reset, restarting\"}");
    delay(500);
    ESP.restart();
}


// ---------------------------------------------------------------------------
// WiFi setup portal API
//
// ESPAsyncWebServer runs handlers on the async TCP task; blocking there stalls
// every other connection and can trip the watchdog. Nothing below waits on the
// radio — scans and connection attempts are queued and driven from wifi_loop().

static const char* wifi_state_name(WifiState st) {
    switch (st) {
        case WIFI_ST_CONNECTING: return "connecting";
        case WIFI_ST_CONNECTED:  return "connected";
        case WIFI_ST_RETRY_WAIT: return "retry_wait";
        case WIFI_ST_SETUP_AP:   return "setup_ap";
        default:                 return "idle";
    }
}

static const char* netcheck_name(NetCheckResult r) {
    switch (r) {
        case NC_OK:   return "ok";
        case NC_FAIL: return "fail";
        default:      return "unknown";
    }
}

// GET /api/wifi/scan — cached results only; scanning is never background-polled
static void handle_wifi_scan(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["scanning"] = wifi_scan_in_progress();
    doc["age_ms"] = wifi_scan_age_ms();

    JsonArray arr = doc["networks"].to<JsonArray>();
    for (int i = 0; i < wifi_scan_count(); i++) {
        const WifiScanEntry* e = wifi_scan_get(i);
        if (!e) continue;
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = e->ssid;
        o["rssi"] = e->rssi;
        o["channel"] = e->channel;
        o["enc"] = e->enc;
        o["enterprise"] = e->enterprise;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/wifi/scan/refresh — kicks off an async scan and returns immediately
static void handle_wifi_scan_refresh(AsyncWebServerRequest* request) {
    bool ok = wifi_scan_start();
    JsonDocument doc;
    doc["started"] = ok;
    doc["scanning"] = wifi_scan_in_progress();
    if (!ok) doc["error"] = "A scan is already running";
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/wifi/status — poll target for the portal page
static void handle_wifi_status(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["state"] = wifi_state_name(wifi_get_state());
    doc["trial"] = wifi_trial_status_str();
    doc["trial_ssid"] = wifi_trial_ssid();
    doc["detail"] = wifi_trial_detail();
    doc["connected"] = wifi_is_connected();
    doc["ip"] = wifi_get_ip();
    doc["rssi"] = wifi_get_rssi();
    doc["ssid"] = config_get().wifi_ssid;
    doc["enterprise"] = config_has_enterprise();
    doc["ap_active"] = wifi_is_ap_mode();
    doc["ap_ssid"] = wifi_get_ap_ssid();
    doc["ap_ip"] = wifi_get_ap_ip();
    doc["ap_stations"] = wifi_ap_station_count();

    JsonObject checks = doc["checks"].to<JsonObject>();
    checks["dns"] = netcheck_name(netcheck_dns());
    checks["data"] = netcheck_name(netcheck_data());
    checks["ntp"] = netcheck_name(netcheck_ntp());
    checks["host"] = netcheck_data_host();
    checks["summary"] = netcheck_summary();
    checks["running"] = netcheck_running();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/wifi/connect — start a trial. Credentials are not persisted here;
// wifi_loop() writes them to NVS only once the join actually succeeds.
static char wifi_body[1024];

static void handle_wifi_connect(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (total > sizeof(wifi_body) - 1) {
        request->send(413, "application/json", "{\"error\":\"Body too large\"}");
        return;
    }
    memcpy(wifi_body + index, data, len);
    if (index + len < total) return;
    wifi_body[total] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, wifi_body, total)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    WifiTrialParams p;
    memset(&p, 0, sizeof(p));
    strncpy(p.ssid, doc["ssid"] | "", sizeof(p.ssid) - 1);
    p.security = doc["security"] | 0;
    p.eap_method = doc["eap_method"] | 0;
    strncpy(p.identity, doc["identity"] | "", sizeof(p.identity) - 1);
    strncpy(p.password, doc["password"] | "", sizeof(p.password) - 1);
    strncpy(p.anon_identity, doc["anon_identity"] | "", sizeof(p.anon_identity) - 1);
    p.validate_ca = doc["validate_ca"] | false;

    if (p.ssid[0] == '\0') {
        request->send(400, "application/json", "{\"error\":\"ssid is required\"}");
        return;
    }
    if (strlen(p.ssid) > 32) {
        request->send(400, "application/json", "{\"error\":\"ssid must be 32 characters or fewer\"}");
        return;
    }
    if (p.security != 0 && p.security != 1) {
        request->send(400, "application/json", "{\"error\":\"unsupported security type\"}");
        return;
    }
    if (p.eap_method != 0 && p.eap_method != 1) {
        request->send(400, "application/json", "{\"error\":\"unsupported EAP method\"}");
        return;
    }
    if (p.security == 1 && p.identity[0] == '\0') {
        request->send(400, "application/json", "{\"error\":\"identity is required for enterprise networks\"}");
        return;
    }

    if (p.validate_ca && !config_ca_exists()) {
        request->send(400, "application/json", "{\"error\":\"upload a CA certificate before enabling validation\"}");
        return;
    }

    if (!wifi_trial_start(p)) {
        request->send(409, "application/json", "{\"error\":\"A connection attempt is already running\"}");
        return;
    }

    request->send(202, "application/json", "{\"status\":\"started\"}");
}

// POST /api/wifi/ca — raw PEM body
static char ca_body[4096];

static void handle_wifi_ca_upload(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (total > sizeof(ca_body) - 1) {
        request->send(413, "application/json", "{\"error\":\"Certificate too large (4 KB max)\"}");
        return;
    }
    memcpy(ca_body + index, data, len);
    if (index + len < total) return;
    ca_body[total] = '\0';

    if (!strstr(ca_body, "-----BEGIN CERTIFICATE-----") ||
        !strstr(ca_body, "-----END CERTIFICATE-----")) {
        request->send(400, "application/json", "{\"error\":\"Expected a PEM certificate\"}");
        return;
    }
    if (!config_ca_write(ca_body, total)) {
        request->send(500, "application/json", "{\"error\":\"Failed to store certificate\"}");
        return;
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// DELETE /api/wifi/ca
static void handle_wifi_ca_delete(AsyncWebServerRequest* request) {
    if (!config_ca_delete()) {
        request->send(500, "application/json", "{\"error\":\"Failed to remove certificate\"}");
        return;
    }
    ConfigGuard guard;
    AppConfig& cfg = config_get();
    cfg.wifi_validate_ca = false;
    config_save();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void webserver_init() {
    // LittleFS is only an optional config/certificate overlay. Embedded web
    // assets keep the UI and firmware atomic and remain available if it fails.
    if (!LittleFS.begin(false)) {
        Serial.println("[WEB] LittleFS mount failed; continuing with embedded UI");
    } else {
        Serial.println("[WEB] LittleFS mounted for config overlay");
    }

    for (size_t i = 0; i < get_web_assets_count(); ++i) {
        const WebAsset* asset = get_web_asset_at(i);
        server.on(asset->path, HTTP_GET, [asset](AsyncWebServerRequest* request) {
            AsyncWebServerResponse* response = request->beginResponse(
                200, asset->mime_type, asset->data, asset->size);
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "no-cache");
            response->addHeader("ETag", asset->etag);
            request->send(response);
        });
    }

    // API routes
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/config", HTTP_GET, handle_get_config);
    server.on("/api/debug", HTTP_GET, handle_debug);
    server.on("/api/history", HTTP_GET, handle_history);
    server.on("/api/timer", HTTP_GET, handle_timer_status);
    server.on("/api/ota/status", HTTP_GET, handle_ota_status);
    server.on("/api/ota/check", HTTP_POST, [](AsyncWebServerRequest* r) {
        send_ota_request_result(r, ota_request_check());
    });
    server.on("/api/ota/install", HTTP_POST, [](AsyncWebServerRequest* r) {
        send_ota_request_result(r, ota_request_install(true));
    });
    server.on("/api/ota/settings", HTTP_POST,
        [](AsyncWebServerRequest* request) {}, NULL, handle_ota_settings);
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* r) { handle_restart(r); });
    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* r) { handle_factory_reset(r); });
    server.on("/api/test/weather", HTTP_POST, [](AsyncWebServerRequest* r) { handle_test_weather(r); });
    server.on("/api/test/glucose", HTTP_GET, [](AsyncWebServerRequest* r) { handle_test_glucose_result(r); });
    server.on("/api/test/glucose", HTTP_POST, [](AsyncWebServerRequest* r) { handle_test_glucose(r); });

    // POST /api/test/weather-mock with body
    server.on("/api/test/weather-mock", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_test_weather_mock
    );
    // WiFi setup portal
    server.on("/api/wifi/scan", HTTP_GET, handle_wifi_scan);
    server.on("/api/wifi/status", HTTP_GET, handle_wifi_status);
    server.on("/api/wifi/scan/refresh", HTTP_POST, [](AsyncWebServerRequest* r) { handle_wifi_scan_refresh(r); });
    server.on("/api/wifi/ca", HTTP_DELETE, [](AsyncWebServerRequest* r) { handle_wifi_ca_delete(r); });
    server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_wifi_connect
    );
    server.on("/api/wifi/ca", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_wifi_ca_upload
    );

    server.on("/api/display/next", HTTP_POST, [](AsyncWebServerRequest* r) { handle_display_next(r); });
    server.on("/api/display/prev", HTTP_POST, [](AsyncWebServerRequest* r) { handle_display_prev(r); });

    // POST /api/config with body
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_post_config
    );

    // POST /api/notify with body
    server.on("/api/notify", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_post_notify
    );

    // POST /api/sysmon with body
    server.on("/api/sysmon", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_post_sysmon
    );

    // Same-origin production UI needs no wildcard CORS. It can be enabled for
    // local development only with -DSUGARCLOCK_DEV_CORS.
#ifdef SUGARCLOCK_DEV_CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
#endif

    // Registered last: this installs the onNotFound catch-all, which must not
    // shadow any real route.
    captive_portal_register_routes(server);

    Serial.println("[WEB] Routes registered");
}

void webserver_start() {
    if (started) return;
    server.begin();
    started = true;
    Serial.printf("[WEB] Server started at http://%s/\n", wifi_get_ip());
}
