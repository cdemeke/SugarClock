#include "buzzer.h"
#include "hardware_pins.h"
#include <Arduino.h>

#define BUZZER_LEDC_CHANNEL 0
#define BUZZER_RESOLUTION   8
#define BEEP_GAP_MS         150  // gap between consecutive beeps

static bool initialized = false;
static int beeps_remaining = 0;
static int beep_freq = 2000;
static int beep_duration_ms = 200;
static unsigned long beep_start_ms = 0;
static bool beep_on = false;

void buzzer_init() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    ledcSetup(BUZZER_LEDC_CHANNEL, 2000, BUZZER_RESOLUTION);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CHANNEL);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    initialized = true;
}

void buzzer_beep(int count, int freq, int duration_ms) {
    if (!initialized) return;
    beeps_remaining = count;
    beep_freq = freq;
    beep_duration_ms = duration_ms;
    // Start first beep immediately
    ledcSetup(BUZZER_LEDC_CHANNEL, beep_freq, BUZZER_RESOLUTION);
    ledcWrite(BUZZER_LEDC_CHANNEL, 128);
    beep_on = true;
    beep_start_ms = millis();
}

void buzzer_loop() {
    if (!initialized || beeps_remaining <= 0) return;

    unsigned long elapsed = millis() - beep_start_ms;

    if (beep_on) {
        // Turn off after duration
        if (elapsed >= (unsigned long)beep_duration_ms) {
            ledcWrite(BUZZER_LEDC_CHANNEL, 0);
            beep_on = false;
            beeps_remaining--;
            beep_start_ms = millis();
        }
    } else {
        // Gap between beeps
        if (beeps_remaining > 0 && elapsed >= BEEP_GAP_MS) {
            ledcSetup(BUZZER_LEDC_CHANNEL, beep_freq, BUZZER_RESOLUTION);
            ledcWrite(BUZZER_LEDC_CHANNEL, 128);
            beep_on = true;
            beep_start_ms = millis();
        }
    }
}

void buzzer_stop() {
    if (!initialized) return;
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    beep_on = false;
    beeps_remaining = 0;
}

bool buzzer_is_active() {
    return beeps_remaining > 0 || beep_on;
}
