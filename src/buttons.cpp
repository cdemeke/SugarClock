#include "buttons.h"
#include "hardware_pins.h"
#include <Arduino.h>

#define DEBOUNCE_MS     50
#define LONG_PRESS_MS   1000

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

void buttons_init() {
    buttons[0] = { PIN_BUTTON_LEFT,   true, true, false, 0, 0, false };
    buttons[1] = { PIN_BUTTON_MIDDLE, true, true, false, 0, 0, false };
    buttons[2] = { PIN_BUTTON_RIGHT,  true, true, false, 0, 0, false };

    // Pins already configured as INPUT_PULLUP in main.cpp
}

void buttons_loop() {
    for (int i = 0; i < 3; i++) {
        bool raw = digitalRead(buttons[i].pin); // LOW = pressed (active LOW)

        // Debounce
        if (raw != buttons[i].last_raw) {
            buttons[i].debounce_time = millis();
        }
        buttons[i].last_raw = raw;

        if ((millis() - buttons[i].debounce_time) < DEBOUNCE_MS) {
            continue; // still bouncing
        }

        bool is_pressed = !raw; // active LOW

        if (is_pressed && !buttons[i].pressed) {
            // Button just pressed
            buttons[i].pressed = true;
            buttons[i].press_start = millis();
            buttons[i].long_fired = false;
        }

        if (is_pressed && buttons[i].pressed && !buttons[i].long_fired) {
            // Check for long press
            if ((millis() - buttons[i].press_start) >= LONG_PRESS_MS) {
                buttons[i].long_fired = true;
                // Fire long press event
                switch (i) {
                    case 0: pending_event = BTN_LEFT_LONG; break;
                    case 1: pending_event = BTN_MIDDLE_LONG; break;
                    case 2: pending_event = BTN_RIGHT_LONG; break;
                }
                Serial.printf("[BTN] Button %d LONG press\n", i);
            }
        }

        if (!is_pressed && buttons[i].pressed) {
            // Button just released
            if (!buttons[i].long_fired) {
                // Short press
                switch (i) {
                    case 0: pending_event = BTN_LEFT_SHORT; break;
                    case 1: pending_event = BTN_MIDDLE_SHORT; break;
                    case 2: pending_event = BTN_RIGHT_SHORT; break;
                }
                Serial.printf("[BTN] Button %d SHORT press\n", i);
            }
            buttons[i].pressed = false;
        }
    }
}

ButtonEvent buttons_get_event() {
    ButtonEvent evt = pending_event;
    pending_event = BTN_NONE;
    return evt;
}
