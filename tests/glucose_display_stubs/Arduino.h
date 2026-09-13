#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

unsigned long millis();
inline long random(long upper) { return upper / 2; }
inline long random(long lower, long upper) { return lower + (upper - lower) / 2; }
struct FakeSerial {
    template<typename... Args> void printf(const char*, Args...) {}
    void println(const char*) {}
};
static FakeSerial Serial;
