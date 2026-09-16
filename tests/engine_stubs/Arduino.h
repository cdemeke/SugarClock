#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
unsigned long millis();
struct TestSerial {
    template<typename... Args> void printf(const char*, Args...) {}
    void println(const char*) {}
};
static TestSerial Serial;
