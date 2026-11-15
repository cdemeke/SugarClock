// Production glucose alarm selection (sliced from src/glucose_engine.cpp by
// test_alarms.py) driving the real buzzer and pattern code. Only the LEDC,
// FreeRTOS lock, display and data sources are faked.
#include <Arduino.h>  // tests/engine_stubs: millis() and Serial
#include <cassert>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

// --- Hardware fakes for src/buzzer.cpp
#define OUTPUT 1
#define LOW 0
struct portMUX_TYPE { bool locked; };
#define portMUX_INITIALIZER_UNLOCKED {false}
#define portENTER_CRITICAL(m) do { assert(!(m)->locked); (m)->locked = true; } while (0)
#define portEXIT_CRITICAL(m) do { (m)->locked = false; } while (0)
static unsigned long now_ms = 1000;
unsigned long millis() { return now_ms; }
static uint32_t ledc_hz = 0, ledc_duty = 0;
static int freq_changes = 0;
void pinMode(int, int) {}
void digitalWrite(int, int) {}
uint32_t ledcSetup(uint8_t, uint32_t hz, uint8_t) { ledc_hz = hz; return hz; }
void ledcAttachPin(uint8_t, uint8_t) {}
void ledcWrite(uint8_t, uint32_t duty) { ledc_duty = duty; }
uint32_t ledcChangeFrequency(uint8_t, uint32_t hz, uint8_t bits) {
    assert(bits == 8);
    ledc_hz = hz;
    ++freq_changes;
    return hz;
}
#include "../src/buzzer.cpp"

// --- Engine dependencies
#include "config_manager.h"
#include "glucose_engine.h"
static AppConfig cfg{};
AppConfig& config_get() { return cfg; }
struct GlucoseReading { bool valid = true; int glucose = 100; };
static GlucoseReading reading;
GlucoseReading http_get_reading() { return reading; }
static unsigned long age_ms = 0;
static int failures = 0;
static bool ever_received = true;
unsigned long http_time_since_last_reading() { return age_ms; }
int http_get_failure_count() { return failures; }
bool http_has_ever_received() { return ever_received; }
#define FAILURE_STALE_COUNT 5
static uint8_t normal_brightness = 40, brightness = 40;
static int shows = 0;
static uint8_t effective_brightness() { return normal_brightness; }
void display_set_brightness(uint8_t value) { brightness = value; }
void display_show() { ++shows; }
static std::vector<std::string> notes;
void notify_push(const char* text, int duration_sec, bool urgent) {
    assert(duration_sec == 2 && !urgent);
    notes.push_back(text);
}
static bool alert_snoozed = false;
static unsigned long alert_snooze_start_ms = 0;
#include "alarm_selection.inc"

// One main-loop iteration: buzzer first, then the engine.
static void loop_once() {
    buzzer_loop();
    update_alarm_flash();
    check_alerts();
}
static void run(unsigned long ms) {
    for (unsigned long i = 0; i < ms; ++i) { loop_once(); ++now_ms; }
}
static bool sounding() { return ledc_duty == 128; }
static AlarmKind due(int glucose) { reading.glucose = glucose; return select_alarm(); }

int main() {
    cfg.glucose_enabled = true; cfg.alert_enabled = true;
    cfg.alert_low = 70; cfg.alert_high = 250; cfg.alert_snooze_min = 15;
    cfg.thresh_urgent_low = 55; cfg.thresh_urgent_high = 300;
    cfg.stale_timeout_min = 20; cfg.data_source = 0;
    cfg.alarm_hz = 2700; cfg.alarm_style = ALARM_STYLE_STEADY;
    buzzer_init();

    // --- Selection
    assert(due(100) == ALARM_NONE && due(70) == ALARM_NONE && due(250) == ALARM_NONE);
    assert(due(54) == ALARM_URGENT && due(40) == ALARM_URGENT);
    assert(due(55) == ALARM_WARNING && due(69) == ALARM_WARNING);
    assert(due(251) == ALARM_WARNING && due(350) == ALARM_WARNING);  // highs never URGENT
    cfg.alert_low = 50; assert(due(52) == ALARM_URGENT); cfg.alert_low = 70;  // unordered thresholds
    cfg.alert_enabled = false; assert(due(40) == ALARM_NONE);
    cfg.alert_enabled = true; cfg.glucose_enabled = false; assert(due(40) == ALARM_NONE);
    cfg.glucose_enabled = true;

    // Briefly stale data is silent; 30 min without new data is a technical alarm.
    age_ms = 20UL * 60000; assert(due(40) == ALARM_NONE);
    age_ms = 0; failures = 5; assert(due(40) == ALARM_NONE);
    age_ms = 30UL * 60000 - 1; assert(due(40) == ALARM_NONE);
    age_ms = 30UL * 60000; assert(due(40) == ALARM_TECHNICAL && due(100) == ALARM_TECHNICAL);
    reading.valid = false; assert(due(100) == ALARM_TECHNICAL);
    ever_received = false; assert(due(100) == ALARM_NONE);  // nothing received yet
    cfg.alert_enabled = false; ever_received = true; assert(due(100) == ALARM_NONE);
    cfg.alert_enabled = true;
    // Pushed data (source 2) never counts as stale.
    cfg.data_source = 2; reading.valid = true;
    assert(due(40) == ALARM_URGENT && due(100) == ALARM_NONE);
    cfg.data_source = 0; age_ms = 0; failures = 0;
    assert(due(100) == ALARM_NONE && !engine_alert_sounding());

    // --- Ordinary beeps play only when nothing else does.
    buzzer_beep(2, 2000, 200);
    run(1); assert(sounding() && ledc_hz == 2000 && buzzer_is_active());
    run(200); assert(!sounding());
    run(150); assert(sounding());
    run(200); assert(!sounding() && !buzzer_is_active());

    // --- URGENT alarm at 50% duty, 2700 Hz, flashing the display even at night.
    normal_brightness = 5;  // night mode
    reading.glucose = 40;
    check_alerts();
    assert(buzzer_is_active() && engine_alert_sounding());
    buzzer_beep(3);  // ignored while the alarm plays
    freq_changes = 0;
    const unsigned long start = now_ms;
    run(1); assert(sounding() && ledc_hz == 2700 && brightness == 200);
    assert(buzzer_active_alarm() == ALARM_URGENT && buzzer_alarm_output_on());
    run(150); assert(!sounding() && brightness == 5);
    run(100); assert(sounding() && brightness == 200);
    run(1300 - 250); assert(!sounding() && brightness == 5 && buzzer_is_active());
    assert(freq_changes == 5);  // pitch set once per pulse
    run(6000 - (now_ms - start)); run(1); assert(sounding());
    normal_brightness = 230; run(1); assert(brightness == 230);  // never dims a bright display
    normal_brightness = 5;
    // Escalates: after a minute it repeats every 3 s.
    run(60000 - (now_ms - start)); run(1); assert(sounding());
    run(2999); assert(!sounding());
    run(1); run(1); assert(sounding());

    // A different kind restarts its own pattern; WARNING does not flash.
    reading.glucose = 65;
    const unsigned long warning_start = now_ms;
    run(2); assert(buzzer_active_alarm() == ALARM_WARNING);
    run(1); assert(sounding() && brightness == 5);
    run(300); assert(!sounding());

    // Saved pitch and style drive the real alarm and apply mid-alarm.
    cfg.alarm_hz = 3000; cfg.alarm_style = ALARM_STYLE_WARBLE;
    run(warning_start + 15000 - now_ms);  // the second WARNING burst
    run(1); assert(sounding() && ledc_hz == 3600);          // warble starts at 1.2x
    run(30); assert(sounding() && ledc_hz == 2550);         // then 0.85x
    assert(buzzer_active_alarm() == ALARM_WARNING);
    cfg.alarm_hz = 2700; cfg.alarm_style = ALARM_STYLE_STEADY;
    run(400);

    // --- Snooze silences at once and survives millis() wrap-around.
    now_ms = ULONG_MAX - 1000;
    run(1000);
    const unsigned long snoozed_at = now_ms;
    engine_snooze_alerts();
    run(1); assert(!sounding() && !buzzer_is_active() && !engine_alert_sounding());
    run(2000);  // millis() wrapped
    assert(now_ms <= 2000 && select_alarm() == ALARM_NONE);
    run(15UL * 60000 - 1 - (now_ms - snoozed_at)); assert(select_alarm() == ALARM_NONE);
    run(1); assert(select_alarm() == ALARM_WARNING);
    run(2); assert(sounding());

    // --- Tests override alarms; snooze stops them.
    reading.glucose = 40;
    run(10000);
    buzzer_test_tone(1000, 500);
    assert(buzzer_test_active() && engine_alert_sounding());  // queued
    run(1); assert(sounding() && ledc_hz == 1000 && buzzer_active_alarm() == ALARM_NONE);
    assert(brightness == 5);  // tone is not an urgent pulse
    run(500); assert(!buzzer_test_active());
    run(1); assert(buzzer_active_alarm() == ALARM_URGENT);  // alarm resumes

    // Drill overrides a real alarm with its own kind and pitch.
    buzzer_test_drill(ALARM_WARNING, 2000, 3000, ALARM_STYLE_STEADY);
    run(2); assert(sounding() && ledc_hz == 3000 && buzzer_active_alarm() == ALARM_WARNING);
    // Snooze while a real alarm is due: stops the drill and snoozes the alarm.
    engine_snooze_alerts();
    run(2); assert(!buzzer_test_active() && !sounding() && select_alarm() == ALARM_NONE);
    alert_snoozed = false;

    // With no alarm due, stopping a drill must not snooze real alarms.
    reading.glucose = 100;
    run(1);
    buzzer_test_drill(ALARM_URGENT, 5000, ALARM_DEFAULT_HZ, ALARM_STYLE_CHIRP);
    run(1); assert(sounding() && ledc_hz == 2240 && brightness == 200);  // chirp, flash
    run(100); assert(ledc_hz > 2800);
    assert(engine_alert_sounding());
    engine_snooze_alerts();
    run(1); assert(!buzzer_test_active() && !sounding() && !alert_snoozed);
    reading.glucose = 40;
    run(2); assert(sounding() && buzzer_active_alarm() == ALARM_URGENT);

    // API stop ends a test without snoozing.
    buzzer_test_tone(500, 10000);
    run(1); assert(ledc_hz == 500);
    buzzer_test_stop();
    run(1); assert(!buzzer_test_active() && buzzer_active_alarm() == ALARM_URGENT);
    reading.glucose = 100;
    run(2); assert(!buzzer_is_active());

    // A drill ends by itself.
    buzzer_test_drill(ALARM_TECHNICAL, 1000, 2000, ALARM_STYLE_STEADY);
    run(1); assert(ledc_hz == 2000);
    run(600); assert(ledc_hz == 1500);
    run(400); assert(!buzzer_test_active() && !sounding());

    // --- Sweep shows each pitch.
    notes.clear();
    buzzer_test_sweep(2500, 3000, 250, 300);
    run(1); assert(ledc_hz == 2500 && buzzer_sweep_hz() == 2500 && notes.size() == 1);
    assert(notes[0] == "2500 Hz");
    buzzer_beep(1);  // ignored during tests
    run(300); assert(ledc_hz == 2750 && notes.back() == "2750 Hz");
    run(300); assert(ledc_hz == 3000 && notes.back() == "3000 Hz");
    run(300); assert(!sounding() && !buzzer_test_active() && buzzer_sweep_hz() == 0);
    assert(notes.size() == 3 && !buzzer_is_active());
    // Downward, with a range that is not a multiple of the step.
    notes.clear();
    buzzer_test_sweep(3000, 2400, 250, 300);
    run(1000);
    assert((notes == std::vector<std::string>{"3000 Hz", "2750 Hz", "2500 Hz"}));
    assert(!buzzer_test_active());
    buzzer_test_sweep(3000, 2400, 0, 300);  // rejected: would never end
    assert(!buzzer_test_active());

    // Glucose off silences everything except an explicit test.
    reading.glucose = 40; cfg.glucose_enabled = false;
    run(10); assert(!buzzer_is_active() && !engine_alert_sounding());
    (void)shows;
    return 0;
}
