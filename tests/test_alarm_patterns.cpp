// Host test for the Arduino-free glucose alarm patterns.
#include "alarm_pattern.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>

// Rising edges (pulse starts) in [from, to), checked every millisecond.
static int pulses(AlarmKind kind, uint32_t from, uint32_t to) {
    int count = 0;
    bool was_on = from > 0 && alarm_pattern_at(kind, from - 1).on;
    for (uint32_t t = from; t < to; ++t) {
        bool on = alarm_pattern_at(kind, t).on;
        if (on && !was_on) ++count;
        was_on = on;
    }
    return count;
}

static bool on(AlarmKind kind, uint32_t t) { return alarm_pattern_at(kind, t).on; }

// Every millisecond of [from, to) has the same state.
static void expect(AlarmKind kind, uint32_t from, uint32_t to, bool state, uint16_t hz = 0) {
    for (uint32_t t = from; t < to; ++t) {
        AlarmTone tone = alarm_pattern_at(kind, t);
        assert(tone.on == state);
        assert(tone.hz == (state ? hz : 0));
    }
}

int main() {
    assert(ALARM_DEFAULT_HZ == 2800);
    assert(!on(ALARM_NONE, 0) && alarm_pattern_at(ALARM_NONE, 0).hz == 0);

    // URGENT: 3+2 burst of 150 ms pulses, 100 ms gaps, 250 ms between groups.
    const uint32_t urgent_on[][2] = {{0, 150}, {250, 400}, {500, 650}, {900, 1050}, {1150, 1300}};
    uint32_t last_end = 0;
    for (auto& p : urgent_on) {
        expect(ALARM_URGENT, last_end, p[0], false);
        expect(ALARM_URGENT, p[0], p[1], true, 2800);
        last_end = p[1];
    }
    expect(ALARM_URGENT, 1300, 6000, false);
    // Every 6 s for the first minute, every 3 s until 3 min, then every 1.7 s.
    assert(pulses(ALARM_URGENT, 0, 60000) == 10 * 5);
    assert(on(ALARM_URGENT, 54000) && on(ALARM_URGENT, 60000) && !on(ALARM_URGENT, 59999));
    assert(pulses(ALARM_URGENT, 60000, 180000) == 40 * 5);
    assert(on(ALARM_URGENT, 63000) && !on(ALARM_URGENT, 62999) && !on(ALARM_URGENT, 61300));
    assert(pulses(ALARM_URGENT, 180000, 180000 + 17000) == 10 * 5);
    assert(on(ALARM_URGENT, 181700) && !on(ALARM_URGENT, 181699) && on(ALARM_URGENT, 181700 + 1150));
    assert(on(ALARM_URGENT, 180000 + 1700 * 1000));  // keeps escalating cadence indefinitely

    // WARNING: 3 pulses of 200 ms with 150 ms gaps; every 15 s, then 8 s after 5 min.
    expect(ALARM_WARNING, 0, 200, true, 2800);
    expect(ALARM_WARNING, 200, 350, false);
    expect(ALARM_WARNING, 350, 550, true, 2800);
    expect(ALARM_WARNING, 550, 700, false);
    expect(ALARM_WARNING, 700, 900, true, 2800);
    expect(ALARM_WARNING, 900, 15000, false);
    assert(pulses(ALARM_WARNING, 0, 300000) == 20 * 3);
    assert(on(ALARM_WARNING, 300000) && !on(ALARM_WARNING, 307999) && on(ALARM_WARNING, 308000));
    assert(pulses(ALARM_WARNING, 300000, 380000) == 10 * 3);

    // TECHNICAL: 500 ms tone, then 700 ms at 0.75x, once a minute.
    expect(ALARM_TECHNICAL, 0, 500, true, 2800);
    expect(ALARM_TECHNICAL, 500, 1200, true, 2100);
    expect(ALARM_TECHNICAL, 1200, 60000, false);
    assert(alarm_pattern_at(ALARM_TECHNICAL, 60000).hz == 2800);
    assert(alarm_pattern_at(ALARM_TECHNICAL, 120700).hz == 2100);

    // Custom pitch.
    assert(alarm_pattern_at(ALARM_URGENT, 0, 1000).hz == 1000);
    assert(alarm_pattern_at(ALARM_TECHNICAL, 600, 4000).hz == 3000);

    // Chirp: each pulse rises from 0.8x to (just under) 1.25x of its pitch.
    for (AlarmKind kind : {ALARM_URGENT, ALARM_WARNING, ALARM_TECHNICAL}) {
        uint16_t previous = 0;
        bool was_on = false;
        for (uint32_t t = 0; t < 2000; ++t) {
            AlarmTone plain = alarm_pattern_at(kind, t);
            AlarmTone tone = alarm_pattern_at(kind, t, ALARM_DEFAULT_HZ, ALARM_STYLE_CHIRP);
            assert(tone.on == plain.on);
            if (!tone.on) { was_on = false; continue; }
            assert(tone.hz >= plain.hz * 8 / 10 && tone.hz < plain.hz * 125 / 100);
            if (!was_on || tone.hz < previous * 9 / 10) {
                assert(tone.hz == plain.hz * 8 / 10);  // a new pulse starts low
            } else {
                assert(tone.hz >= previous);
            }
            previous = tone.hz;
            was_on = true;
        }
    }
    assert(alarm_pattern_at(ALARM_URGENT, 0, 2700, ALARM_STYLE_CHIRP).hz == 2160);
    assert(alarm_pattern_at(ALARM_URGENT, 149, 2700, ALARM_STYLE_CHIRP).hz == 3366);
    assert(alarm_pattern_at(ALARM_TECHNICAL, 500, 2700, ALARM_STYLE_CHIRP).hz == 1620);
    assert(alarm_pattern_at(ALARM_URGENT, 149, 8000, ALARM_STYLE_CHIRP).hz == 9976);  // fits uint16_t

    // Every style keeps the on/off timing; only the pitch moves.
    for (AlarmKind kind : {ALARM_URGENT, ALARM_WARNING, ALARM_TECHNICAL}) {
        for (uint8_t style = 0; style < ALARM_STYLE_COUNT; ++style) {
            for (uint32_t t = 0; t < 200000; t += 7) {
                AlarmTone tone = alarm_pattern_at(kind, t, 2700, (AlarmStyle)style);
                assert(tone.on == on(kind, t));
                if (tone.on) assert(tone.hz >= 2025 * 3 / 4 && tone.hz <= 3375);
            }
        }
    }
    // Warble: 1.2x and 0.85x, swapping every 30 ms within the pulse.
    assert(alarm_pattern_at(ALARM_URGENT, 0, 2700, ALARM_STYLE_WARBLE).hz == 3240);
    assert(alarm_pattern_at(ALARM_URGENT, 29, 2700, ALARM_STYLE_WARBLE).hz == 3240);
    assert(alarm_pattern_at(ALARM_URGENT, 30, 2700, ALARM_STYLE_WARBLE).hz == 2295);
    assert(alarm_pattern_at(ALARM_URGENT, 60, 2700, ALARM_STYLE_WARBLE).hz == 3240);
    assert(alarm_pattern_at(ALARM_URGENT, 250, 2700, ALARM_STYLE_WARBLE).hz == 3240);  // each pulse restarts
    // Siren: 0.75x up to 1.25x and back every 120 ms.
    assert(alarm_pattern_at(ALARM_WARNING, 0, 2000, ALARM_STYLE_SIREN).hz == 1500);
    assert(alarm_pattern_at(ALARM_WARNING, 30, 2000, ALARM_STYLE_SIREN).hz == 2000);
    assert(alarm_pattern_at(ALARM_WARNING, 60, 2000, ALARM_STYLE_SIREN).hz == 2500);
    assert(alarm_pattern_at(ALARM_WARNING, 90, 2000, ALARM_STYLE_SIREN).hz == 2000);
    assert(alarm_pattern_at(ALARM_WARNING, 120, 2000, ALARM_STYLE_SIREN).hz == 1500);
    // Mix: steady, chirp, warble, siren on successive repeats, then around again.
    const AlarmStyle mix_order[] = {ALARM_STYLE_STEADY, ALARM_STYLE_CHIRP, ALARM_STYLE_WARBLE,
                                    ALARM_STYLE_SIREN};
    const uint32_t first_burst_ms = 30;  // inside the first pulse of a burst
    for (uint32_t repeat = 0; repeat < 10; ++repeat) {
        const uint32_t t = repeat * 6000 + first_burst_ms;
        const AlarmStyle expected = mix_order[repeat % 4];
        assert(alarm_pattern_at(ALARM_URGENT, t, 2700, ALARM_STYLE_MIX).hz ==
               alarm_pattern_at(ALARM_URGENT, t, 2700, expected).hz);
    }
    assert(alarm_pattern_at(ALARM_URGENT, 180000 + 1700 * 2 + 40, 2700, ALARM_STYLE_MIX).hz ==
           alarm_pattern_at(ALARM_URGENT, 180000 + 1700 * 2 + 40, 2700, ALARM_STYLE_WARBLE).hz);
    assert(alarm_pattern_at(ALARM_URGENT, 0, 8000, ALARM_STYLE_SIREN).hz == 6000);

    // Continuous preview: same pitches as inside a pulse, with no gaps.
    for (uint32_t t = 0; t < 3000; ++t) assert(alarm_style_hz(ALARM_STYLE_STEADY, t, 2700) == 2700);
    assert(alarm_style_hz(ALARM_STYLE_SIREN, 1260, 2000) == 2500);
    assert(alarm_style_hz(ALARM_STYLE_CHIRP, 0, 2700) == 2160 && alarm_style_hz(ALARM_STYLE_CHIRP, 400, 2700) == 2160);
    assert(alarm_style_hz(ALARM_STYLE_CHIRP, 399, 2700) > 3300);
    assert(alarm_style_hz(ALARM_STYLE_WARBLE, 3030, 2000) == 1700);
    for (uint32_t second = 0; second < 12; ++second) {
        const uint32_t t = second * 1000 + 30;
        assert(alarm_style_hz(ALARM_STYLE_MIX, t, 2700) == alarm_style_hz(mix_order[second % 4], t, 2700));
    }
    for (uint8_t style = 0; style < ALARM_STYLE_COUNT; ++style) {
        for (uint32_t t = 0; t < 10000; ++t) {
            const uint16_t hz = alarm_style_hz((AlarmStyle)style, t, 2700);
            assert(hz >= 2025 && hz <= 3375);
        }
    }

    // Names.
    assert(alarm_kind_from_name("urgent") == ALARM_URGENT);
    assert(alarm_kind_from_name("warning") == ALARM_WARNING);
    assert(alarm_kind_from_name("technical") == ALARM_TECHNICAL);
    assert(alarm_kind_from_name("none") == ALARM_NONE);
    assert(alarm_kind_from_name("URGENT") == ALARM_NONE);
    assert(alarm_kind_from_name("") == ALARM_NONE && alarm_kind_from_name(nullptr) == ALARM_NONE);
    for (AlarmKind kind : {ALARM_URGENT, ALARM_WARNING, ALARM_TECHNICAL}) {
        assert(alarm_kind_from_name(alarm_kind_name(kind)) == kind);
    }
    assert(alarm_kind_name((AlarmKind)9)[0] == 'n');
    assert(alarm_style_from_name("steady") == ALARM_STYLE_STEADY);
    assert(alarm_style_from_name("chirp") == ALARM_STYLE_CHIRP);
    assert(alarm_style_from_name("warble") == ALARM_STYLE_WARBLE);
    assert(alarm_style_from_name("siren") == ALARM_STYLE_SIREN);
    assert(alarm_style_from_name("mix") == ALARM_STYLE_MIX);
    assert(alarm_style_from_name("hop") == ALARM_STYLE_COUNT);  // removed
    assert(ALARM_STYLE_MIX == 4 && ALARM_STYLE_COUNT == 5);  // saved values keep their meaning
    assert(alarm_style_from_name("loud") == ALARM_STYLE_COUNT && alarm_style_from_name(nullptr) == ALARM_STYLE_COUNT);
    return 0;
}
