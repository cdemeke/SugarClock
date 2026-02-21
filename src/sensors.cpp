#include "sensors.h"
#include "hardware_pins.h"
#include <Arduino.h>

#define SENSOR_UPDATE_MS    2000
#define LDR_SAMPLES         10
#define BRIGHTNESS_MIN      5
#define BRIGHTNESS_MAX      200

// Battery voltage divider calibration
// TC001 uses voltage divider, typical factor ~2.0
#define BATTERY_DIVIDER     2.0f
#define ADC_REF_VOLTAGE     3.3f
#define ADC_RESOLUTION      4095.0f

static int ldr_readings[LDR_SAMPLES];
static int ldr_index = 0;
static bool ldr_filled = false;
static int ldr_smoothed = 2048;
static float battery_voltage = 0;
static unsigned long last_update_ms = 0;

void sensors_init() {
    // Initialize ADC pins
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db); // Full range 0-3.3V

    // Pre-fill LDR buffer
    for (int i = 0; i < LDR_SAMPLES; i++) {
        ldr_readings[i] = analogRead(PIN_LDR);
    }
    ldr_filled = true;

    // Initial battery read
    battery_voltage = (analogRead(PIN_BATTERY) / ADC_RESOLUTION) * ADC_REF_VOLTAGE * BATTERY_DIVIDER;

    Serial.printf("[SENSOR] LDR: %d, Battery: %.2fV\n", ldr_smoothed, battery_voltage);
}

void sensors_loop() {
    if (millis() - last_update_ms < SENSOR_UPDATE_MS) return;
    last_update_ms = millis();

    // Read LDR with rolling average
    ldr_readings[ldr_index] = analogRead(PIN_LDR);
    ldr_index = (ldr_index + 1) % LDR_SAMPLES;
    if (ldr_index == 0) ldr_filled = true;

    int sum = 0;
    int count = ldr_filled ? LDR_SAMPLES : ldr_index;
    for (int i = 0; i < count; i++) {
        sum += ldr_readings[i];
    }
    ldr_smoothed = sum / count;

    // Read battery
    battery_voltage = (analogRead(PIN_BATTERY) / ADC_RESOLUTION) * ADC_REF_VOLTAGE * BATTERY_DIVIDER;
}

int sensors_get_ldr() {
    return ldr_smoothed;
}

uint8_t sensors_get_auto_brightness() {
    // Map LDR (0=dark, 4095=bright) to brightness
    // Higher LDR = more ambient light = need brighter display
    int brightness = map(ldr_smoothed, 0, 4095, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    return constrain(brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
}

float sensors_get_battery_voltage() {
    return battery_voltage;
}

int sensors_get_battery_percent() {
    // Rough LiPo estimate: 3.0V = 0%, 4.2V = 100%
    if (battery_voltage <= 0) return -1; // no battery / not connected
    int pct = (int)((battery_voltage - 3.0f) / 1.2f * 100.0f);
    return constrain(pct, 0, 100);
}
