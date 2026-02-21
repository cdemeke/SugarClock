#ifndef BUTTONS_H
#define BUTTONS_H

// Button event types
enum ButtonEvent {
    BTN_NONE,
    BTN_LEFT_SHORT,
    BTN_LEFT_LONG,
    BTN_MIDDLE_SHORT,
    BTN_MIDDLE_LONG,
    BTN_RIGHT_SHORT,
    BTN_RIGHT_LONG
};

// Initialize buttons with debouncing
void buttons_init();

// Poll buttons - call every loop iteration
void buttons_loop();

// Get and consume the latest button event
ButtonEvent buttons_get_event();

#endif // BUTTONS_H
