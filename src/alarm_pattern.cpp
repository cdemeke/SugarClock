#include "alarm_pattern.h"
#include <string.h>

namespace {

struct Pulse {
    uint16_t start_ms;
    uint16_t len_ms;
    uint16_t pitch_permille;  // of the base pitch
};

// 3+2 burst: 150 ms pulses, 100 ms gaps, 250 ms between the groups.
const Pulse URGENT_PULSES[] = {
    {0, 150, 1000}, {250, 150, 1000}, {500, 150, 1000},
    {900, 150, 1000}, {1150, 150, 1000},
};
// Three 200 ms pulses with 150 ms gaps.
const Pulse WARNING_PULSES[] = {
    {0, 200, 1000}, {350, 200, 1000}, {700, 200, 1000},
};
// 500 ms tone, then 700 ms at 0.75x: a falling "no data" cue.
const Pulse TECHNICAL_PULSES[] = {
    {0, 500, 1000}, {500, 700, 750},
};

// Position within the current repeat, and which repeat it is. Each
// escalation step starts on a whole number of earlier periods, so the pattern
// never jumps mid-burst.
uint32_t cycle_position(AlarmKind kind, uint32_t t, uint32_t& repeat) {
    uint32_t start = 0, period = 60000UL;                 // TECHNICAL: once a minute
    if (kind == ALARM_URGENT) {
        if (t < 60000UL) period = 6000UL;                 // every 6 s for 1 min
        else if (t < 180000UL) start = 60000UL, period = 3000UL;  // every 3 s until 3 min
        else start = 180000UL, period = 1700UL;           // then every 1.7 s
    } else if (kind == ALARM_WARNING) {
        if (t < 300000UL) period = 15000UL;               // every 15 s for 5 min
        else start = 300000UL, period = 8000UL;           // then every 8 s
    }
    repeat = (t - start) / period;
    return (t - start) % period;
}

// Pitch multiplier in thousandths, `off` ms into a pulse of `len` ms.
uint32_t style_permille(AlarmStyle style, uint32_t off, uint32_t len) {
    switch (style) {
        case ALARM_STYLE_CHIRP:  return 800 + 450 * off / len;
        case ALARM_STYLE_WARBLE: return (off / 30) & 1 ? 850 : 1200;
        case ALARM_STYLE_SIREN: {
            const uint32_t phase = off % 120;
            return 750 + 500 * (phase < 60 ? phase : 120 - phase) / 60;
        }
        default: return 1000;
    }
}

}  // namespace

AlarmTone alarm_pattern_at(AlarmKind kind, uint32_t elapsed_ms, uint16_t base_hz, AlarmStyle style) {
    const Pulse* pulses;
    unsigned count;
    switch (kind) {
        case ALARM_URGENT:    pulses = URGENT_PULSES;    count = 5; break;
        case ALARM_WARNING:   pulses = WARNING_PULSES;   count = 3; break;
        case ALARM_TECHNICAL: pulses = TECHNICAL_PULSES; count = 2; break;
        default: return {false, 0};
    }
    uint32_t repeat;
    const uint32_t pos = cycle_position(kind, elapsed_ms, repeat);
    // Mix rotates steady, chirp, warble, siren so no repeat sounds the same.
    if (style == ALARM_STYLE_MIX) style = (AlarmStyle)(repeat % ALARM_STYLE_MIX);
    for (unsigned i = 0; i < count; ++i) {
        const Pulse& p = pulses[i];
        if (pos < p.start_ms || pos >= (uint32_t)p.start_ms + p.len_ms) continue;
        const uint32_t hz = (uint32_t)base_hz * p.pitch_permille / 1000;
        return {true, (uint16_t)(hz * style_permille(style, pos - p.start_ms, p.len_ms) / 1000)};
    }
    return {false, 0};
}

uint16_t alarm_style_hz(AlarmStyle style, uint32_t elapsed_ms, uint16_t base_hz) {
    // One endless tone, shaped as back-to-back 400 ms pulses; Mix changes
    // style every second.
    if (style == ALARM_STYLE_MIX) style = (AlarmStyle)((elapsed_ms / 1000) % ALARM_STYLE_MIX);
    return (uint16_t)((uint32_t)base_hz * style_permille(style, elapsed_ms % 400, 400) / 1000);
}

static const char* const ALARM_NAMES[] = {"none", "urgent", "warning", "technical"};

AlarmKind alarm_kind_from_name(const char* name) {
    for (uint8_t k = ALARM_URGENT; k <= ALARM_TECHNICAL; ++k) {
        if (name && strcmp(name, ALARM_NAMES[k]) == 0) return (AlarmKind)k;
    }
    return ALARM_NONE;
}

const char* alarm_kind_name(AlarmKind kind) {
    return kind <= ALARM_TECHNICAL ? ALARM_NAMES[kind] : "none";
}

static const char* const STYLE_NAMES[] = {"steady", "chirp", "warble", "siren", "mix"};

AlarmStyle alarm_style_from_name(const char* name) {
    for (uint8_t i = 0; i < ALARM_STYLE_COUNT; ++i) {
        if (name && strcmp(name, STYLE_NAMES[i]) == 0) return (AlarmStyle)i;
    }
    return ALARM_STYLE_COUNT;
}
