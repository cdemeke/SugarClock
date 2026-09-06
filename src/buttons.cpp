#include "buttons.h"
#include "double_click_detector.h"
#include "hardware_pins.h"
#include <Arduino.h>

#define DEBOUNCE_MS     50
#define LONG_PRESS_MS   1000
#define DOUBLE_CLICK_MS 350

struct ButtonState {
    uint8_t pin;
    bool last_raw;
    bool stable;
    bool pressed;
    unsigned long debounce_time;
    unsigned long press_start;
    bool long_fired;
};

static ButtonState buttons[3];
static ButtonEvent pending_event = BTN_NONE;
static DoubleClickDetector middle_click(DOUBLE_CLICK_MS);
static bool middle_exclusive = false;

static void publish_middle_click(DoubleClickResult result) {
    if (result == DOUBLE_CLICK_SINGLE) {
        pending_event = BTN_MIDDLE_SHORT;
        Serial.println("[BTN] Button 1 SHORT press");
    } else if (result == DOUBLE_CLICK_DOUBLE) {
        pending_event = BTN_MIDDLE_DOUBLE;
        Serial.println("[BTN] Button 1 DOUBLE press");
    }
}

void buttons_init() {
    pending_event = BTN_NONE;
    middle_click.cancel();
    middle_exclusive = false;
    buttons[0] = { PIN_BUTTON_LEFT,   true, true, false, 0, 0, false };
    buttons[1] = { PIN_BUTTON_MIDDLE, true, true, false, 0, 0, false };
    buttons[2] = { PIN_BUTTON_RIGHT,  true, true, false, 0, 0, false };

    pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
    pinMode(PIN_BUTTON_MIDDLE, INPUT_PULLUP);
    pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
}

void buttons_loop() {
    unsigned long now = millis();
    // The outer-button chord is the TC001 hardware power shortcut. Pairing
    // uses only the middle button, with its action selected on release.
    if (!digitalRead(PIN_BUTTON_LEFT) || !digitalRead(PIN_BUTTON_RIGHT)) {
        middle_exclusive = false;
    }
    for (int i = 0; i < 3; i++) {
        bool raw = digitalRead(buttons[i].pin); // LOW = pressed (active LOW)

        // Debounce
        if (raw != buttons[i].last_raw) {
            buttons[i].debounce_time = now;
        }
        buttons[i].last_raw = raw;

        if ((now - buttons[i].debounce_time) < DEBOUNCE_MS) {
            continue; // still bouncing
        }

        bool is_pressed = !raw; // active LOW

        if (is_pressed && !buttons[i].pressed) {
            // Button just pressed
            buttons[i].pressed = true;
            buttons[i].press_start = now;
            buttons[i].long_fired = false;
            if (i == 1) {
                middle_exclusive = digitalRead(PIN_BUTTON_LEFT) && digitalRead(PIN_BUTTON_RIGHT);
                publish_middle_click(middle_click.on_press(now));
            }
        }

        if (is_pressed && buttons[i].pressed && !buttons[i].long_fired) {
            // Check for long press
            if ((now - buttons[i].press_start) >= LONG_PRESS_MS) {
                buttons[i].long_fired = true;
                if (i == 1) middle_click.cancel();
                // Fire long press event
                switch (i) {
                    case 0: pending_event = BTN_LEFT_LONG; break;
                    // Wait for release to distinguish snooze, pairing and
                    // bond reset without suppressing alerts during pairing.
                    case 1: break;
                    case 2: pending_event = BTN_RIGHT_LONG; break;
                }
                Serial.printf("[BTN] Button %d LONG press\n", i);
            }
        }

        if (!is_pressed && buttons[i].pressed) {
            // Button just released. No middle action fires while held.
            if (i == 1 && buttons[i].long_fired && middle_exclusive) {
                unsigned long held = now - buttons[i].press_start;
                pending_event = held >= 10000 ? BTN_BOND_RESET :
                                held >= 3000 ? BTN_PAIRING : BTN_MIDDLE_LONG;
            }
            if (!buttons[i].long_fired) {
                if (i == 1) {
                    // Delay the middle-button single press just long enough to
                    // distinguish it from the new double-click shortcut.
                    publish_middle_click(middle_click.on_short_release(now));
                } else {
                    switch (i) {
                        case 0: pending_event = BTN_LEFT_SHORT; break;
                        case 2: pending_event = BTN_RIGHT_SHORT; break;
                    }
                    Serial.printf("[BTN] Button %d SHORT press\n", i);
                }
            }
            buttons[i].pressed = false;
        }
    }

    // Do not consume a pending middle click while another event is waiting.
    // The main loop drains pending_event on every iteration.
    if (pending_event == BTN_NONE) {
        publish_middle_click(middle_click.poll(now, buttons[1].pressed));
    }
}

ButtonEvent buttons_get_event() {
    ButtonEvent evt = pending_event;
    pending_event = BTN_NONE;
    return evt;
}
