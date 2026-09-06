#pragma once
#include <stdint.h>
#define INPUT_PULLUP 2
unsigned long millis();
int digitalRead(uint8_t pin);
void pinMode(uint8_t pin, int mode);
struct TestSerial {
    void println(const char*) {}
    template<class... Args> void printf(const char*, Args...) {}
};
extern TestSerial Serial;
