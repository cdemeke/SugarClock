#ifndef GLUCOSE_FRESHNESS_H
#define GLUCOSE_FRESHNESS_H
#include <stdint.h>

// Allow two minutes beyond the five-minute sensor cadence for upload/poll delay.
// A shorter configured stale timeout or repeated failures still takes precedence.
inline bool glucose_display_is_stale(int source, unsigned long age_ms,
                                     int timeout_min, int failures) {
    return source != 2 && (age_ms > 420000UL ||
           age_ms >= (unsigned long)timeout_min * 60000UL || failures >= 5);
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
