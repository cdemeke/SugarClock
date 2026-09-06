#include "Arduino.h"
#include "buttons.h"
#include "hardware_pins.h"
#include <cassert>

TestSerial Serial;
static unsigned long now;
static bool pins[40];
unsigned long millis() {return now;}
int digitalRead(uint8_t pin) {return pins[pin];}
void pinMode(uint8_t, int) {}
static void tick(unsigned long elapsed) {now+=elapsed;buttons_loop();}
static void reset() {
    now=100;
    for(auto& pin:pins) pin=true;
    buttons_init();
}
static void press(uint8_t pin) {pins[pin]=false;tick(1);tick(50);}
static void release(uint8_t pin) {pins[pin]=true;tick(1);tick(50);}
static void middleHold(unsigned long duration,ButtonEvent expected) {
    reset();press(PIN_BUTTON_MIDDLE);
    tick(1000);assert(buttons_get_event()==BTN_NONE); // No premature snooze.
    tick(duration-1000);assert(buttons_get_event()==BTN_NONE);
    release(PIN_BUTTON_MIDDLE);assert(buttons_get_event()==expected);
    tick(500);assert(buttons_get_event()==BTN_NONE); // No brightness change.
}
int main() {
    middleHold(1500,BTN_MIDDLE_LONG);
    middleHold(3100,BTN_PAIRING);
    middleHold(9000,BTN_PAIRING);
    middleHold(10100,BTN_BOND_RESET);
    reset();press(PIN_BUTTON_MIDDLE);release(PIN_BUTTON_MIDDLE);
    assert(buttons_get_event()==BTN_NONE);
    tick(350);assert(buttons_get_event()==BTN_MIDDLE_SHORT);
    reset();press(PIN_BUTTON_MIDDLE);release(PIN_BUTTON_MIDDLE);
    tick(100);press(PIN_BUTTON_MIDDLE);release(PIN_BUTTON_MIDDLE);
    assert(buttons_get_event()==BTN_MIDDLE_DOUBLE);
    tick(500);assert(buttons_get_event()==BTN_NONE);
    // A long second press cancels the pending double/single click.
    reset();press(PIN_BUTTON_MIDDLE);release(PIN_BUTTON_MIDDLE);
    tick(100);press(PIN_BUTTON_MIDDLE);tick(3100);
    assert(buttons_get_event()==BTN_NONE);
    release(PIN_BUTTON_MIDDLE);assert(buttons_get_event()==BTN_PAIRING);
    // Outer-button power gestures never admit phones or erase bonds.
    reset();press(PIN_BUTTON_LEFT);press(PIN_BUTTON_RIGHT);tick(11000);
    auto event=buttons_get_event();assert(event!=BTN_PAIRING && event!=BTN_BOND_RESET);
    release(PIN_BUTTON_LEFT);release(PIN_BUTTON_RIGHT);
    event=buttons_get_event();assert(event!=BTN_PAIRING && event!=BTN_BOND_RESET);
    // Pairing must be a middle-only gesture, even if a side button is released first.
    reset();press(PIN_BUTTON_MIDDLE);press(PIN_BUTTON_LEFT);tick(11000);
    buttons_get_event();release(PIN_BUTTON_LEFT);release(PIN_BUTTON_MIDDLE);
    assert(buttons_get_event()==BTN_NONE);
    // Existing side-button actions remain.
    reset();press(PIN_BUTTON_LEFT);release(PIN_BUTTON_LEFT);
    assert(buttons_get_event()==BTN_LEFT_SHORT);
    reset();press(PIN_BUTTON_RIGHT);tick(1000);
    assert(buttons_get_event()==BTN_RIGHT_LONG);
}
