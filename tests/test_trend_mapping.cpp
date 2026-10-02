#include "trend_mapping.h"
#include "libre_session.h"
#include "display.h"
#include "FastLED_NeoMatrix.h"
#include <cassert>
#include <cstring>
#include <string>
#include <cctype>

FakeLEDs FastLED;
unsigned long millis() { return 0; }

// Fixed source fixtures: no comparison of independently sampled live readings.
// Dexcom: https://github.com/gagebenne/pydexcom/blob/main/pydexcom/const.py
// Libre: https://github.com/timoschlueter/nightscout-librelink-up/blob/main/src/helpers/helpers.ts
struct Fixture {
    int dexcom;
    const char* name;
    TrendType trend;
    const char* rows[7];
};
static const Fixture fixtures[] = {
    {1, "DoubleUp", TREND_RISING_FAST, {".X.X.", "XXXXX", ".X.X.", ".X.X.", ".X.X.", ".X.X.", ".X.X."}},
    {2, "SingleUp", TREND_RISING, {"..X..", ".XXX.", "X.X.X", "..X..", "..X..", "..X..", "..X.."}},
    {3, "FortyFiveUp", TREND_FORTY_FIVE_UP, {"XXXXX", "...XX", "..X.X", ".X..X", "X...X", ".....", "....."}},
    {4, "Flat", TREND_FLAT, {".....", "..X..", "...X.", "XXXXX", "...X.", "..X..", "....."}},
    {5, "FortyFiveDown", TREND_FORTY_FIVE_DOWN, {".....", ".....", "X...X", ".X..X", "..X.X", "...XX", "XXXXX"}},
    {6, "SingleDown", TREND_FALLING, {"..X..", "..X..", "..X..", "..X..", "X.X.X", ".XXX.", "..X.."}},
    {7, "DoubleDown", TREND_FALLING_FAST, {".X.X.", ".X.X.", ".X.X.", ".X.X.", ".X.X.", "XXXXX", ".X.X."}},
};

static void check_pixels(const Fixture& f, int x, int y) {
    bool actual[8][32] = {};
    for (const auto& p : drawing().pixels) {
        assert(p.x >= x && p.x < x + 5 && p.y >= y && p.y < y + 7);
        actual[p.y][p.x] = true;
    }
    for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col)
            assert(actual[y + row][x + col] == (f.rows[row][col] == 'X'));
}

int main() {
    display_init();
    for (const auto& f : fixtures) {
        assert(parse_trend_number(f.dexcom) == f.trend);
        assert(parse_trend(f.name) == f.trend);
        assert(parse_trend(TREND_NAMES[f.trend]) == f.trend);
        std::string lower = f.name;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(ch));
        assert(parse_trend(lower.c_str()) == f.trend);
        display_clear();
        display_draw_trend(parse_trend_number(f.dexcom), 23, 1, 1);
        check_pixels(f, 23, 1);
        display_clear();
        display_draw_glucose_delta(5, parse_trend(f.name), 1, false);
        check_pixels(f, 1, 0);
    }
    const TrendType libre[] = {TREND_UNKNOWN, TREND_FALLING, TREND_FORTY_FIVE_DOWN,
                              TREND_FLAT, TREND_FORTY_FIVE_UP, TREND_RISING};
    for (int id = 0; id <= 5; ++id) assert(libre_map_trend(id) == libre[id]);
    assert(libre_map_trend(-1) == TREND_UNKNOWN);
    assert(libre_map_trend(6) == TREND_UNKNOWN);
    for (int id : {-1, 0, 8, 9, 999}) assert(parse_trend_number(id) == TREND_UNKNOWN);
    for (const char* name : {"", "Unknown", "NONE", "NOT COMPUTABLE", "RATE OUT OF RANGE", "invalid"})
        assert(parse_trend(name) == TREND_UNKNOWN);
    assert(parse_trend(nullptr) == TREND_UNKNOWN);
    assert(std::strcmp(TREND_NAMES[TREND_UNKNOWN], "Unknown") == 0);
    for (int id : {-1, static_cast<int>(TREND_UNKNOWN), 999}) {
        display_clear();
        display_draw_trend(id, 0, 0, 1);
        assert(drawing().pixels.empty());
    }
}
