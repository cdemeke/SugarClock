#ifndef GLUCOSE_FRESHNESS_H
#define GLUCOSE_FRESHNESS_H
#include <stdint.h>

constexpr int GLUCOSE_DEMO_SOURCE = 2;
constexpr unsigned long GLUCOSE_VISUAL_STALE_MS = 420000UL;
constexpr unsigned long GLUCOSE_SENSOR_CADENCE_SEC = 300UL;
constexpr unsigned long GLUCOSE_DELIVERY_GRACE_SEC = 60UL;

// Accommodate sensor cadence, one poll interval and delivery grace. The
// configured alert timeout remains an independent upper bound on freshness.
inline bool glucose_display_is_stale(int source, unsigned long age_ms,
                                     int timeout_min, int failures,
                                     int poll_interval_sec = 60) {
    const uint64_t poll_sec = poll_interval_sec > 0 ? uint64_t(poll_interval_sec) : 0;
    const uint64_t cadence_ms = (GLUCOSE_SENSOR_CADENCE_SEC + poll_sec +
                                 GLUCOSE_DELIVERY_GRACE_SEC) * 1000;
    const uint64_t visual_ms = cadence_ms > GLUCOSE_VISUAL_STALE_MS
        ? cadence_ms : GLUCOSE_VISUAL_STALE_MS;
    return source != GLUCOSE_DEMO_SOURCE && (age_ms > visual_ms ||
           uint64_t(age_ms) >= uint64_t(timeout_min) * 60000 || failures >= 5);
}

// Preserve elapsed age for repeated sensor timestamps, including before NTP
// sync. With a trusted wall clock, include age already accrued before polling.
// Timestamp-less generic endpoints retain their receive-time fallback.
inline uint32_t glucose_received_at(uint32_t timestamp, uint32_t previous_timestamp,
                                   bool have_previous, uint32_t previous_ms,
                                   uint32_t now_ms, bool clock_synced, uint32_t epoch) {
    uint32_t age = (have_previous && timestamp != 0 && timestamp == previous_timestamp)
        ? now_ms - previous_ms : 0;
    if (timestamp != 0 && clock_synced && epoch >= timestamp) {
        uint64_t sensor_age = uint64_t(epoch - timestamp) * 1000;
        // Bound the pre-boot offset to avoid overflowing millis arithmetic.
        if (sensor_age > 0x7fffffffUL) sensor_age = 0x7fffffffUL;
        if (sensor_age > age) age = uint32_t(sensor_age);
    }
    return now_ms - age;
}
#endif
