#include <cassert>
#include <string>
#include "FastLED_NeoMatrix.h"
// Exercise the real engine and shared drawing/scrolling code with fake devices.
// Including the engine also lets scenarios start part-way through a screen fade.
#include "../src/glucose_engine.cpp"

FakeLEDs FastLED;
static uint32_t now_ms = 10000;
static AppConfig config = {};
static GlucoseReading reading = {};
static WeatherReading weather = {};
static OtaDisplayLifecycle lifecycle;
static WeatherPreFetchCallback weather_callback = nullptr;
static int hour = 12;
static int beeps = 0;

unsigned long millis() { return now_ms; }
AppConfig& config_get() { return config; }
bool config_has_wifi() { return true; }
bool config_has_server() { return true; }
const GlucoseReading& http_get_reading() { return reading; }
int http_get_failure_count() { return 0; }
unsigned long http_time_since_last_reading() { return 0; }
bool http_has_ever_received() { return true; }
int http_get_delta() { return 0; }
bool wifi_is_connected() { return true; }
bool wifi_is_ap_mode() { return false; }
const char* wifi_get_ip() { return "192.0.2.1"; }
const char* wifi_get_ap_ip() { return "192.0.2.2"; }
const char* wifi_get_ap_ssid() { return "Test"; }
WifiTrialState wifi_trial_get_state() { return WIFI_TRIAL_IDLE; }
const char* wifi_trial_ssid() { return "Test"; }
bool time_is_available() { return true; }
int time_get_hour() { return hour; }
int time_get_minute() { return 30; }
int time_get_second() { return 0; }
int time_get_day() { return 1; }
int time_get_month() { return 1; }
const char* time_get_month_abbr() { return "JAN"; }
uint8_t sensors_get_auto_brightness() { return 65; }
void buzzer_beep(int, int, int) { ++beeps; }
const WeatherReading& weather_get_reading() { return weather; }
bool weather_has_data() { return true; }
void weather_set_pre_fetch_callback(WeatherPreFetchCallback cb) { weather_callback = cb; }
void ambient_fish_init() {}
void ambient_fish_render() {}
void ambient_fish_interact() {}
TimerState timer_get_state() { return TIMER_IDLE; }
int timer_get_remaining_sec() { return 0; }
void timer_toggle_start_pause() {}
void timer_reset() {}
StopwatchState stopwatch_get_state() { return SW_IDLE; }
int stopwatch_get_elapsed_sec() { return 0; }
void stopwatch_toggle_start_pause() {}
void stopwatch_reset() {}
bool notify_has_active() { return false; }
const char* notify_get_text() { return "Test"; }
bool notify_is_urgent() { return false; }
bool sysmon_has_data() { return false; }
int sysmon_get_value() { return 0; }
int sysmon_get_max() { return 100; }
const char* sysmon_get_label() { return "CPU"; }
long countdown_get_remaining_sec() { return 0; }
bool countdown_is_configured() { return false; }
NetCheckResult netcheck_dns() { return NC_OK; }
NetCheckResult netcheck_data() { return NC_OK; }
const char* netcheck_summary() { return ""; }
OtaDisplayPhase ota_get_display_phase() { return lifecycle.phase(now_ms); }
void ota_display_frame_shown(OtaDisplayPhase phase) { lifecycle.frame_shown(phase, now_ms); }

static std::string drawn_text() {
    std::string result;
    for (const auto& glyph : drawing().glyphs) result += glyph.value;
    return result;
}

static void tick(uint32_t elapsed = 50) {
    now_ms += elapsed;
    int before = FastLED.shows;
    engine_loop();
    assert(FastLED.shows == before + 1); // Only one frame reaches the LEDs.
}

static void test_lifecycle() {
    OtaDisplayLifecycle state;
    // General errors cannot create the failure overlay before installation.
    state.fail(100);
    assert(state.phase(100) == OTA_DISPLAY_NONE);
    assert(!state.reboot_ready(10000));
    state.start();
    assert(state.phase(200) == OTA_DISPLAY_UPDATING);
    state.verify();
    assert(state.phase(300) == OTA_DISPLAY_VERIFYING);
    state.fail(400);
    assert(state.phase(8399) == OTA_DISPLAY_FAILED);
    assert(state.phase(8400) == OTA_DISPLAY_NONE);
    state.start();
    state.fail(UINT32_MAX - 3999);
    assert(state.phase(3999) == OTA_DISPLAY_FAILED);
    assert(state.phase(4000) == OTA_DISPLAY_NONE);

    state.start();
    state.reboot(100);
    state.frame_shown(OTA_DISPLAY_UPDATING, 200); // A stale frame is not BOOT.
    assert(!state.reboot_ready(1100));
    state.frame_shown(OTA_DISPLAY_REBOOTING, 500);
    state.frame_shown(OTA_DISPLAY_REBOOTING, 1000); // Do not restart dwell time.
    assert(!state.reboot_ready(1499));
    assert(state.reboot_ready(1500));
    state.reboot(UINT32_MAX - 999);
    assert(!state.reboot_ready(1999));
    assert(state.reboot_ready(2000)); // Bounded even with no rendering callback.
}

static void test_engine() {
    config.data_source = 2;
    config.brightness = 40;
    config.alert_enabled = true;
    config.alert_low = 70;
    config.alert_high = 200;
    config.alert_snooze_min = 10;
    config.auto_cycle_enabled = true;
    config.auto_cycle_sec = 1;
    reading.valid = true;
    reading.glucose = 60;
    reading.force_mode = -1;
    display_init();
    engine_init();
    boot_start_ms = 0;
    current_state = STATE_WEATHER_DISPLAY;
    user_mode = STATE_WEATHER_DISPLAY;
    config.weather_enabled = true;
    engine_rebuild_toggle_order();
    transition_phase = TRANSITION_FADE_OUT;
    transition_level = 0;
    transition_start_level = 0;
    display_set_transition_level(0);
    lifecycle.start();
    tick();
    assert(drawn_text() == "Updating...");
    assert(FastLED.brightness == 40);
    assert(beeps == 1); // The overlay did not bypass the alert check.
    int shows = FastLED.shows;
    weather_callback();
    assert(FastLED.shows == shows);

    tick(700);
    int scroll_x = drawing().glyphs.front().x;
    lifecycle.verify();
    tick(70);
    assert(drawn_text() == "Updating...");
    assert(drawing().glyphs.front().x == scroll_x - 1);
    assert(engine_get_user_mode() == STATE_WEATHER_DISPLAY);
    assert(transition_phase == TRANSITION_NONE);
    config.auto_brightness = true;
    tick();
    assert(FastLED.brightness == 65);
    config.night_mode_enabled = true;
    config.night_start_hour = 22;
    config.night_end_hour = 7;
    config.night_brightness = 10;
    hour = 23;
    tick();
    assert(FastLED.brightness == 10);

    engine_snooze_alerts();
    tick(10000);
    assert(beeps == 1);
    assert(engine_get_user_mode() == STATE_WEATHER_DISPLAY); // No auto cycling.
    lifecycle.fail(now_ms);
    tick();
    assert(drawn_text() == "Update failed");
    for (int frame = 0; frame < 154; ++frame) {
        tick();
        assert(drawn_text() == "Update failed");
    }
    // A full pass has completed before the failure deadline expires.
    assert(drawing().glyphs.front().x == MATRIX_WIDTH);
    tick(250);
    assert(ota_get_display_phase() == OTA_DISPLAY_NONE);
    assert(drawn_text() != "Update failed");
    assert(FastLED.brightness == 10);
    assert(engine_get_user_mode() == STATE_WEATHER_DISPLAY);

    lifecycle.start(); // A retry starts a fresh marquee.
    tick();
    assert(drawn_text() == "Updating...");
    assert(drawing().glyphs.front().x == MATRIX_WIDTH);
    lifecycle.reboot(now_ms);
    tick();
    assert(drawn_text() == "BOOT");
    for (const auto& glyph : drawing().glyphs) {
        assert(glyph.x >= 0 && glyph.x + 4 < MATRIX_WIDTH);
    }
    assert(!lifecycle.reboot_ready(now_ms + 999));
    assert(lifecycle.reboot_ready(now_ms + 1000));
}

int main() {
    test_lifecycle();
    test_engine();
}
