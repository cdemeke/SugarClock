#include "notify_engine.h"
#include "config_manager.h"
#include "buzzer.h"
#include <Arduino.h>
#include <string.h>

#define MAX_NOTIFICATIONS 3

struct Notification {
    char text[64];
    unsigned long expire_ms;
    bool urgent;
    bool active;
};

static Notification notifications[MAX_NOTIFICATIONS];
static int current_index = 0;

void notify_init() {
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        notifications[i].active = false;
        notifications[i].text[0] = '\0';
    }
}

void notify_loop() {
    // Expire old notifications
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (notifications[i].active && millis() >= notifications[i].expire_ms) {
            notifications[i].active = false;
            Serial.printf("[NOTIFY] Notification %d expired\n", i);
        }
    }
}

void notify_push(const char* text, int duration_sec, bool urgent) {
    // Find empty slot or use oldest (circular)
    int slot = -1;
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (!notifications[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        // Overwrite oldest (slot 0, shift others)
        for (int i = 0; i < MAX_NOTIFICATIONS - 1; i++) {
            notifications[i] = notifications[i + 1];
        }
        slot = MAX_NOTIFICATIONS - 1;
    }

    strncpy(notifications[slot].text, text, sizeof(notifications[slot].text) - 1);
    notifications[slot].text[sizeof(notifications[slot].text) - 1] = '\0';
    notifications[slot].expire_ms = millis() + (unsigned long)duration_sec * 1000UL;
    notifications[slot].urgent = urgent;
    notifications[slot].active = true;
    current_index = slot;

    Serial.printf("[NOTIFY] Pushed: \"%s\" duration=%ds urgent=%d\n", text, duration_sec, urgent);

    // Buzzer for urgent notifications
    AppConfig& cfg = config_get();
    if (urgent && cfg.notify_allow_buzzer) {
        buzzer_beep(2, 2500, 150);
    }
}

bool notify_has_active() {
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (notifications[i].active) return true;
    }
    return false;
}

const char* notify_get_text() {
    // Return most recent active notification
    for (int i = MAX_NOTIFICATIONS - 1; i >= 0; i--) {
        if (notifications[i].active) {
            return notifications[i].text;
        }
    }
    return "";
}

bool notify_is_urgent() {
    for (int i = MAX_NOTIFICATIONS - 1; i >= 0; i--) {
        if (notifications[i].active) {
            return notifications[i].urgent;
        }
    }
    return false;
}

void notify_dismiss() {
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        notifications[i].active = false;
    }
}
