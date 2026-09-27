#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
using std::min;
using std::max;
extern std::atomic<uint32_t> test_time_offset;
inline unsigned long millis() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(test_time_offset.load() +
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
}
inline long random(long lo, long) { return lo + 1; }
template<class T> T constrain(T x, T lo, T hi) { return std::max(lo, std::min(x, hi)); }
struct TestSerial {
    template<class... Args> void printf(const char*, Args...) {}
    void println(const char*) {}
};
static TestSerial Serial;
struct TestESP { void restart() { std::abort(); } };
static TestESP ESP;
class String : public std::string {
public:
    using std::string::string;
    String(const std::string& value):std::string(value) {}
    void trim() {}
    void replace(const char*, const char*) {}
};
struct StopNetworkLoop {};
inline void vTaskDelay(unsigned long) { throw StopNetworkLoop(); }
using BaseType_t = int;
#define pdPASS 1
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, int, void*, int, void*, int) { return pdPASS; }
