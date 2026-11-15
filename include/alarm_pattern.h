#ifndef ALARM_PATTERN_H
#define ALARM_PATTERN_H

// Glucose alarm sound patterns. Pure functions with no Arduino dependency, so
// the timing can be unit tested on the host.

#include <stdint.h>

#define ALARM_DEFAULT_HZ 2800  // near the TC001 buzzer's loudest resonance

enum AlarmKind : uint8_t {
    ALARM_NONE = 0,
    ALARM_URGENT,     // urgent low: IEC 60601-1-8 style 3+2 burst, escalating
    ALARM_WARNING,    // low or high: 3 pulses, escalating
    ALARM_TECHNICAL,  // no new data: two falling tones once a minute
};

// How the pitch moves within each pulse. Changing pitch keeps a passive
// buzzer crossing its loudest resonance and is harder to sleep through.
enum AlarmStyle : uint8_t {
    ALARM_STYLE_STEADY = 0,  // fixed pitch
    ALARM_STYLE_CHIRP,       // each pulse rises from 0.8x to 1.25x
    ALARM_STYLE_WARBLE,      // trills between 1.2x and 0.85x every 30 ms
    ALARM_STYLE_SIREN,       // sweeps 0.75x-1.25x-0.75x every 120 ms
    ALARM_STYLE_MIX,         // a different one of the above on every repeat
    ALARM_STYLE_COUNT
};

struct AlarmTone {
    bool on;
    uint16_t hz;  // 0 when off
};

// Buzzer output `elapsed_ms` after the alarm started.
AlarmTone alarm_pattern_at(AlarmKind kind, uint32_t elapsed_ms,
                           uint16_t base_hz = ALARM_DEFAULT_HZ,
                           AlarmStyle style = ALARM_STYLE_STEADY);

// Pitch of `style` played as one continuous tone, for previewing a style.
uint16_t alarm_style_hz(AlarmStyle style, uint32_t elapsed_ms, uint16_t base_hz);

// "urgent" / "warning" / "technical" -> kind; anything else -> ALARM_NONE.
AlarmKind alarm_kind_from_name(const char* name);
const char* alarm_kind_name(AlarmKind kind);
// "steady" / "chirp" / "warble" / "siren" / "mix" -> style; else ALARM_STYLE_COUNT.
AlarmStyle alarm_style_from_name(const char* name);

#endif // ALARM_PATTERN_H
