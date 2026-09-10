#pragma once
#include <stdint.h>
#include <stdio.h>

namespace glucose_format_detail {
enum class TextKind { Value, Delta };

// Readings, deltas and thresholds stay in mg/dL internally. Convert only text
// at the display boundary, to one decimal place, without floating-point drift.
inline void format(char* out, size_t capacity, int mgdl, bool mmol, TextKind kind) {
    const bool signed_delta = kind == TextKind::Delta;
    if (!mmol) {
        snprintf(out, capacity, signed_delta ? "%+d" : "%d", mgdl);
        return;
    }
    const int64_t value = mgdl;
    const int64_t magnitude = value < 0 ? -value : value;
    const int64_t tenths = (magnitude * 5 + 4) / 9; // mg/dL / 18, rounded to tenths.
    const char* sign = value < 0 ? "-" : signed_delta ? "+" : "";
    snprintf(out, capacity, "%s%lld.%lld", sign,
             static_cast<long long>(tenths / 10), static_cast<long long>(tenths % 10));
}
} // namespace glucose_format_detail

inline void format_glucose_value(char* out, size_t capacity, int mgdl, bool mmol) {
    glucose_format_detail::format(out, capacity, mgdl, mmol, glucose_format_detail::TextKind::Value);
}

inline void format_glucose_delta(char* out, size_t capacity, int mgdl, bool mmol) {
    glucose_format_detail::format(out, capacity, mgdl, mmol, glucose_format_detail::TextKind::Delta);
}
