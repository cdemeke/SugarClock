// Compile the complete production engine; fake only its hardware/services.
#include "../src/glucose_engine.cpp"
#include <cassert>
#include <initializer_list>

static AppConfig cfg = {};
static GlucoseReading reading = {};
static unsigned long now_ms = 0, age_ms = 0;
static int failures = 0, drawn_glucose = 0, delta_frames = 0, button_actions = 0;
static uint16_t drawn_color = 0;
static uint8_t frame_level = 0;
static bool connected = true, ap_mode = false, notification = false, ever_received = true;
static NetCheckResult network = NC_OK;

unsigned long millis() { return now_ms; }
AppConfig& config_get() { return cfg; }
bool config_has_wifi() { return true; }
bool config_has_server() { return true; }
const GlucoseReading& http_get_reading() { return reading; }
unsigned long http_time_since_last_reading() { return age_ms; }
int http_get_failure_count() { return failures; }
bool http_has_ever_received() { return ever_received; }
int http_get_delta() { return -5; }
int http_get_history(GlucoseHistoryEntry*, int) { return 0; }
bool wifi_is_connected() { return connected; }
bool wifi_is_ap_mode() { return ap_mode; }
const char* wifi_get_ip() { return "192.0.2.1"; }
const char* wifi_get_ap_ip() { return "192.0.2.2"; }
const char* wifi_get_ap_ssid() { return "SugarClock"; }
int wifi_ap_station_count() { return 0; }
WifiTrialState wifi_trial_get_state() { return WIFI_TRIAL_IDLE; }
const char* wifi_trial_ssid() { return "test"; }
NetCheckResult netcheck_dns() { return network; }
NetCheckResult netcheck_data() { return network; }
const char* netcheck_summary() { return "offline"; }
bool notify_has_active() { return notification; }
bool notify_is_urgent() { return true; }
const char* notify_get_text() { return "message"; }
bool time_is_available() { return true; }
int time_get_hour() { return 12; }
int time_get_minute() { return 0; }
int time_get_second() { return 0; }
int time_get_day() { return 1; }
int time_get_month() { return 1; }
const char* time_get_month_abbr() { return "JAN"; }
uint8_t sensors_get_auto_brightness() { return 40; }
void buzzer_beep(int, int, int) {}
void ambient_fish_init() {}
void ambient_fish_render() {}
void ambient_fish_interact() { ++button_actions; }
void timer_toggle_start_pause() { ++button_actions; }
void timer_reset() { ++button_actions; }
TimerState timer_get_state() { return TIMER_IDLE; }
int timer_get_remaining_sec() { return 60; }
void stopwatch_toggle_start_pause() { ++button_actions; }
void stopwatch_reset() { ++button_actions; }
StopwatchState stopwatch_get_state() { return SW_IDLE; }
int stopwatch_get_elapsed_sec() { return 0; }
bool sysmon_has_data() { return true; }
int sysmon_get_value() { return 50; }
int sysmon_get_max() { return 100; }
const char* sysmon_get_label() { return "CPU"; }
bool countdown_is_configured() { return false; }
long countdown_get_remaining_sec() { return 0; }
bool weather_has_data() { return false; }
const WeatherReading& weather_get_reading() { static WeatherReading wx = {}; return wx; }
void weather_set_pre_fetch_callback(WeatherPreFetchCallback) {}
void display_set_brightness(uint8_t) {}
void display_set_transition_level(uint8_t level) { frame_level = level; }
void display_clear() {}
void display_show() {}
void display_scroll_reset() {}
bool display_scroll_text(const char*, int, uint16_t, unsigned int) { return false; }
void display_draw_text(const char*, int, int, uint16_t) {}
void display_draw_pixel(int, int, uint16_t) {}
void display_draw_time(int, int, bool, bool, uint16_t) {}
void display_draw_trend(int, int, int, uint16_t) {}
void display_draw_bar(int, int, uint16_t) {}
int display_draw_glucose(int value, uint16_t color, bool) {
    drawn_glucose = value; drawn_color = color; return 24;
}
uint16_t display_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}
void display_draw_glucose_delta(int, int, uint16_t, bool) {}
void glucose_render_delta_flash(int, uint16_t, bool) { ++delta_frames; }
void glucose_render_stale(const GlucoseReading&, const AppConfig&, uint8_t) {}

static void tick(unsigned long elapsed = 50) { now_ms += elapsed; engine_loop(); }
static void settle() { tick(); tick(600); tick(600); }
static void assert_low_frame() {
    delta_frames = 0;
    tick();
    assert(engine_get_state() == STATE_GLUCOSE_DISPLAY);
    assert(drawn_glucose == reading.glucose);
    assert(frame_level == 255);
    assert(delta_frames == 0);
}

int main() {
    cfg.thresh_low = 80; cfg.thresh_urgent_low = 70; cfg.thresh_urgent_high = 250;
    cfg.stale_timeout_min = 20; cfg.auto_cycle_sec = 3;
    cfg.time_display_enabled = true; cfg.weather_enabled = true;
    cfg.timer_enabled = true; cfg.stopwatch_enabled = true;
    cfg.ambient_enabled = true; cfg.sysmon_enabled = true; cfg.countdown_enabled = true;
    cfg.notify_enabled = true; cfg.show_delta = true; cfg.default_mode = 1;
    cfg.color_low = 0xFFAA00; cfg.color_stale = 0x808080;
    reading.valid = true; reading.glucose = 75; reading.force_mode = -1;
    engine_init(); tick(6000); settle();
    // Off by default: low readings don't change normal screen selection.
    assert(engine_get_state() == STATE_TIME_DISPLAY);
    notification = true; settle();
    assert(engine_get_state() == STATE_NOTIFY_DISPLAY);

    cfg.glucose_only_when_low = true;
    cfg.auto_cycle_enabled = true;
    // Enter during a partially completed fade and delta flash.
    transition_phase = TRANSITION_FADE_OUT; transition_level = 80;
    delta_flash_active = true; delta_flash_start_ms = now_ms;
    last_seen_glucose = 100;
    engine_force_state(STATE_WEATHER_DISPLAY);
    reading.force_mode = STATE_AMBIENT_CREATURE_DISPLAY;
    strcpy(reading.message, "hello");
    assert_low_frame();
    assert_low_frame();
    for (int mode = STATE_BOOT; mode <= STATE_AMBIENT_CREATURE_DISPLAY; ++mode) {
        engine_force_state(static_cast<DisplayState>(mode));
        reading.force_mode = mode;
        assert_low_frame();
    }

    // Both navigation directions, address overlay, and contextual buttons stay
    // inactive, preserving the pre-low screen even over many rotation intervals.
    const DisplayState saved_mode = engine_get_user_mode();
    for (int i = 0; i < 10; ++i) {
        engine_toggle_mode(); engine_toggle_mode_prev(); engine_show_connection_info();
        engine_right_button_action(); engine_right_long_action(); tick(5000);
        assert(engine_get_user_mode() == saved_mode);
        assert(engine_get_state() == STATE_GLUCOSE_DISPLAY);
        assert(!connection_info_visible);
    }
    for (DisplayState mode : {STATE_TIMER_DISPLAY, STATE_STOPWATCH_DISPLAY, STATE_AMBIENT_CREATURE_DISPLAY}) {
        engine_set_default_mode(mode);
        engine_right_button_action(); engine_right_long_action();
        assert(button_actions == 0);
    }
    engine_set_default_mode(saved_mode); engine_rebuild_toggle_order();

    // Actual glucose updates keep rendering; urgent lows use the same lock.
    for (int glucose : {79, 70, 69, 55, 78}) {
        reading.glucose = glucose; assert_low_frame();
    }
    cfg.use_mmol = true; assert_low_frame();
    // Keep stale low data marked stale, including loss of network/setup mode.
    age_ms = 20UL * 60000; failures = 10;
    connected = false; ap_mode = true; network = NC_FAIL;
    assert_low_frame();
    assert(drawn_color == display_color(128, 128, 128));
    cfg.data_source = 2; assert_low_frame();
    assert(drawn_color == display_color(255, 170, 0));
    cfg.data_source = 0;

    // Invalid/cleared readings don't lock the display or invent a low value.
    reading.valid = false;
    assert(!low_glucose_display_is_active());
    assert(evaluate_state() == STATE_SETUP_AP);
    reading.valid = true;
    connected = true; ap_mode = false; network = NC_OK; age_ms = 0; failures = 0;
    engine_clear_force(); reading.force_mode = -1; reading.message[0] = '\0';
    notification = false;

    // At the exact low threshold, restore the saved screen for a full interval.
    tick(); reading.glucose = 80; settle();
    assert(engine_get_state() == saved_mode);
    tick(100); assert(engine_get_state() == saved_mode);
    tick(3000); settle(); assert(engine_get_state() != saved_mode);
    // Updated thresholds take effect without rebooting, with strict boundaries.
    cfg.thresh_low = 90; reading.glucose = 89; assert_low_frame();
    reading.glucose = 90; assert(!low_glucose_display_is_active());
    reading.glucose = 300; assert(!low_glucose_display_is_active());
    // Preserve urgent-low handling even with misordered configured thresholds.
    cfg.thresh_low = 60; reading.glucose = 65; assert_low_frame();
    // Disabling the option while low restores normal override precedence.
    cfg.glucose_only_when_low = false; notification = true; settle();
    assert(engine_get_state() == STATE_NOTIFY_DISPLAY);
    notification = false; cfg.auto_cycle_enabled = false;
    engine_force_state(STATE_WEATHER_DISPLAY); settle();
    assert(engine_get_state() == STATE_WEATHER_DISPLAY);
}
