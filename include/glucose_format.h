#pragma once
#include <stdint.h>
#include <stdio.h>

// Readings, deltas and thresholds stay in mg/dL internally. Convert only text
// at the display boundary, to one decimal place, without floating-point drift.
inline void format_glucose(char* out, size_t capacity, int mgdl, bool mmol, bool delta=false) {
    if (!mmol) {
        snprintf(out, capacity, delta ? "%+d" : "%d", mgdl);
        return;
    }
    const int64_t value=mgdl;
    const int64_t magnitude=value<0 ? -value : value;
    const int64_t tenths=(magnitude*5+4)/9; // mg/dL / 18, rounded to tenths.
    const char* sign=value<0 ? "-" : delta ? "+" : "";
    snprintf(out, capacity, "%s%lld.%lld", sign,
             static_cast<long long>(tenths/10), static_cast<long long>(tenths%10));
}
