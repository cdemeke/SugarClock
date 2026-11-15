#include "buzzer.h"
#include "hardware_pins.h"
#include <Arduino.h>

#define BUZZER_LEDC_CHANNEL 0
#define BUZZER_RESOLUTION   8
#define BUZZER_DUTY_HALF    128  // 50% duty: the loudest a passive buzzer gets
#define BEEP_GAP_MS         150  // gap between consecutive beeps

enum SoundTestType : uint8_t { TEST_NONE, TEST_DRILL, TEST_TONE, TEST_SWEEP, TEST_STOP };

struct SoundTest {
    SoundTestType type;
    AlarmKind kind;
    AlarmStyle style;
    uint16_t hz, to_hz, step_hz;
    uint32_t ms;  // total duration (drill/tone) or per step (sweep)
};

static bool initialized = false;
static uint16_t output_hz = 0;  // 0 = silent

// Ordinary beeps (lowest priority)
static int beep_count = 0;
static int beep_freq = 2000;
static int beep_duration_ms = 200;
static unsigned long beep_start_ms = 0;

// Glucose alarm (middle priority)
static AlarmKind alarm_kind = ALARM_NONE;
static uint16_t alarm_hz = ALARM_DEFAULT_HZ;
static AlarmStyle alarm_style = ALARM_STYLE_STEADY;
static unsigned long alarm_start_ms = 0;

// Sound tests (highest priority). Web handlers run on another task, so they
// only write the pending request under this lock.
static portMUX_TYPE test_mux = portMUX_INITIALIZER_UNLOCKED;
static SoundTest pending_test = {};
static volatile bool test_pending = false;
static SoundTest test = {};
static unsigned long test_start_ms = 0;

static AlarmKind audible_alarm = ALARM_NONE;
static bool alarm_output_on = false;
static uint16_t sweep_hz = 0;

static void drive(uint16_t hz) {
    if (hz == output_hz) return;
    if (hz == 0) {
        ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    } else {
        ledcChangeFrequency(BUZZER_LEDC_CHANNEL, hz, BUZZER_RESOLUTION);
        ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_DUTY_HALF);
    }
    output_hz = hz;
}

void buzzer_init() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    ledcSetup(BUZZER_LEDC_CHANNEL, ALARM_DEFAULT_HZ, BUZZER_RESOLUTION);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CHANNEL);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    initialized = true;
}

void buzzer_beep(int count, int freq, int duration_ms) {
    if (!initialized || count <= 0 || alarm_kind != ALARM_NONE || buzzer_test_active()) return;
    beep_count = count;
    beep_freq = freq;
    beep_duration_ms = duration_ms;
    beep_start_ms = millis();
}

// True while the running test lasts; sets hz (0 in a drill's silent gaps).
static bool run_test(unsigned long now, uint16_t& hz) {
    const uint32_t t = now - test_start_ms;
    if (test.type == TEST_SWEEP) {
        const bool up = test.to_hz >= test.hz;
        const int32_t step_hz = (int32_t)(t / test.ms) * test.step_hz;
        const int32_t f = up ? test.hz + step_hz : test.hz - step_hz;
        if (up ? f <= test.to_hz : f >= test.to_hz) {
            hz = sweep_hz = (uint16_t)f;
            return true;
        }
    } else if (t < test.ms) {
        if (test.type == TEST_TONE) {
            hz = alarm_style_hz(test.style, t, test.hz);
        } else {
            AlarmTone tone = alarm_pattern_at(test.kind, t, test.hz, test.style);
            audible_alarm = test.kind;
            alarm_output_on = tone.on;
            hz = tone.hz;
        }
        return true;
    }
    Serial.println("[BUZZER] Sound test finished");
    test.type = TEST_NONE;
    return false;
}

void buzzer_loop() {
    if (!initialized) return;
    const unsigned long now = millis();

    if (test_pending) {
        portENTER_CRITICAL(&test_mux);
        test = pending_test;
        test_pending = false;
        portEXIT_CRITICAL(&test_mux);
        if (test.type == TEST_STOP) test.type = TEST_NONE;
        test_start_ms = now;
        beep_count = 0;
    }

    audible_alarm = ALARM_NONE;
    alarm_output_on = false;
    sweep_hz = 0;
    uint16_t hz = 0;

    if (test.type != TEST_NONE && run_test(now, hz)) {
        // Tests override the glucose alarm and beeps.
    } else if (alarm_kind != ALARM_NONE) {
        audible_alarm = alarm_kind;
        AlarmTone tone = alarm_pattern_at(alarm_kind, now - alarm_start_ms, alarm_hz, alarm_style);
        alarm_output_on = tone.on;
        hz = tone.hz;
    } else if (beep_count > 0) {
        const uint32_t t = now - beep_start_ms;
        const uint32_t cycle = beep_duration_ms + BEEP_GAP_MS;
        if (t >= cycle * beep_count - BEEP_GAP_MS) beep_count = 0;
        else if (t % cycle < (uint32_t)beep_duration_ms) hz = beep_freq;
    }
    drive(hz);
}

void buzzer_stop() {
    beep_count = 0;
}

bool buzzer_is_active() {
    return beep_count > 0 || alarm_kind != ALARM_NONE || buzzer_test_active() || output_hz != 0;
}

void buzzer_set_alarm(AlarmKind kind, uint16_t hz, AlarmStyle style) {
    alarm_hz = hz;
    alarm_style = style;
    if (kind == alarm_kind) return;
    Serial.printf("[BUZZER] Alarm: %s -> %s\n", alarm_kind_name(alarm_kind), alarm_kind_name(kind));
    alarm_kind = kind;
    alarm_start_ms = millis();
    beep_count = 0;  // an alarm replaces any ordinary beeps
}

AlarmKind buzzer_active_alarm() {
    return audible_alarm;
}

bool buzzer_alarm_output_on() {
    return alarm_output_on;
}

static void queue_test(const SoundTest& request) {
    portENTER_CRITICAL(&test_mux);
    pending_test = request;
    test_pending = true;
    portEXIT_CRITICAL(&test_mux);
}

void buzzer_test_drill(AlarmKind kind, uint32_t duration_ms, uint16_t hz, AlarmStyle style) {
    queue_test({TEST_DRILL, kind, style, hz, 0, 0, duration_ms});
}

void buzzer_test_tone(uint16_t hz, uint32_t duration_ms, AlarmStyle style) {
    queue_test({TEST_TONE, ALARM_NONE, style, hz, 0, 0, duration_ms});
}

void buzzer_test_sweep(uint16_t from_hz, uint16_t to_hz, uint16_t step_hz, uint32_t step_ms) {
    if (step_hz == 0 || step_ms == 0) return;
    queue_test({TEST_SWEEP, ALARM_NONE, ALARM_STYLE_STEADY, from_hz, to_hz, step_hz, step_ms});
}

void buzzer_test_stop() {
    queue_test({TEST_STOP, ALARM_NONE, ALARM_STYLE_STEADY, 0, 0, 0, 0});
}

bool buzzer_test_active() {
    return test.type != TEST_NONE || (test_pending && pending_test.type != TEST_STOP);
}

uint16_t buzzer_sweep_hz() {
    return sweep_hz;
}
