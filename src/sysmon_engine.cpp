#include "sysmon_engine.h"
#include <Arduino.h>
#include <string.h>

#define SYSMON_STALE_MS 30000  // 30 seconds

static char label[8] = "CPU";
static int value = 0;
static int max_val = 100;
static unsigned long last_push_ms = 0;

void sysmon_init() {
    label[0] = '\0';
    value = 0;
    max_val = 100;
    last_push_ms = 0;
}

void sysmon_loop() {
    // Nothing needed - staleness is checked in sysmon_has_data()
}

void sysmon_push(const char* lbl, int val, int mx) {
    strncpy(label, lbl, sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    value = val;
    max_val = (mx > 0) ? mx : 100;
    last_push_ms = millis();
    Serial.printf("[SYSMON] Push: %s=%d/%d\n", label, value, max_val);
}

bool sysmon_has_data() {
    if (last_push_ms == 0) return false;
    return (millis() - last_push_ms) < SYSMON_STALE_MS;
}

const char* sysmon_get_label() {
    return label;
}

int sysmon_get_value() {
    return value;
}

int sysmon_get_max() {
    return max_val;
}
