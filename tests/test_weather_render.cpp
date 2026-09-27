#include "weather_render.h"
#include "display.h"

#include <array>
#include <assert.h>
#include <cmath>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits>

using Frame = std::array<std::array<uint16_t, 32>, 8>;
static Frame screen;
static const uint16_t TEXT = 0xf81f;

void display_draw_pixel(int x, int y, uint16_t color) {
    // Reject off-panel writes even if the real display library would clip them.
    assert(x >= 0 && x < 32 && y >= 0 && y < 8);
    screen[y][x] = color;
}
uint16_t display_color(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}
static Frame render(float temp, bool fahrenheit, int condition, uint32_t ms) {
    screen = {};
    weather_render(temp, fahrenheit, condition, ms, TEXT);
    for (int y = 0; y < 8; ++y) {
        assert(screen[y][8] == 0 && screen[y][9] == 0);
        for (int x = 10; x < 32; ++x)
            assert(screen[y][x] == 0 || screen[y][x] == TEXT);
    }
    return screen;
}
static void same_region(const Frame& a, const Frame& b, int first, int last) {
    for (int y = 0; y < 8; ++y)
        for (int x = first; x <= last; ++x) assert(a[y][x] == b[y][x]);
}
static bool differs(const Frame& a, const Frame& b, int first, int last) {
    for (int y = 0; y < 8; ++y)
        for (int x = first; x <= last; ++x)
            if (a[y][x] != b[y][x]) return true;
    return false;
}
static void golden_icon(const Frame& frame, const char* const (&rows)[8]) {
    for (int y = 0; y < 8; ++y) {
        assert(strlen(rows[y]) == 8);
        for (int x = 0; x < 8; ++x) assert((frame[y][x] != 0) == (rows[y][x] == '#'));
    }
}
static void mapping() {
    const struct { int id; WeatherVisual visual; } cases[] = {
        {200, WeatherVisual::Storm}, {201, WeatherVisual::Storm}, {232, WeatherVisual::Storm},
        {300, WeatherVisual::Drizzle}, {321, WeatherVisual::Drizzle},
        {500, WeatherVisual::Rain}, {501, WeatherVisual::Rain}, {531, WeatherVisual::Rain},
        {511, WeatherVisual::Sleet}, {611, WeatherVisual::Sleet}, {612, WeatherVisual::Sleet},
        {613, WeatherVisual::Sleet}, {615, WeatherVisual::Sleet}, {616, WeatherVisual::Sleet},
        {600, WeatherVisual::Snow}, {601, WeatherVisual::Snow}, {602, WeatherVisual::Snow},
        {620, WeatherVisual::Snow}, {621, WeatherVisual::Snow}, {622, WeatherVisual::Snow},
        {701, WeatherVisual::Rain}, {721, WeatherVisual::Rain}, {741, WeatherVisual::Rain},
        {711, WeatherVisual::CloudFallback}, {731, WeatherVisual::CloudFallback},
        {751, WeatherVisual::CloudFallback}, {761, WeatherVisual::CloudFallback},
        {762, WeatherVisual::CloudFallback}, {771, WeatherVisual::CloudFallback},
        {781, WeatherVisual::Tornado}, {800, WeatherVisual::Clear},
        {801, WeatherVisual::PartlyCloudy}, {802, WeatherVisual::PartlyCloudy},
        {803, WeatherVisual::Cloudy}, {804, WeatherVisual::Cloudy},
        {0, WeatherVisual::CloudFallback}, {-1, WeatherVisual::CloudFallback},
        {9999, WeatherVisual::CloudFallback}
    };
    for (const auto& item : cases) assert(weather_visual_for_condition(item.id) == item.visual);
}
static void golden_temperatures() {
    // Expected complete text rows transcribed from the approved tall design.
    const struct { float value; bool fahrenheit; int x; const char* rows[7]; } cases[] = {
        {7, true, 15, {"1111.11.111", "0001.11.100", "0010.00.100", "0010.00.110", "0100.00.100", "0100.00.100", "0100.00.100"}},
        {-12, false, 11, {"000.0010.0110.11.011", "000.0110.1001.11.100", "000.0010.0001.00.100", "111.0010.0010.00.100", "000.0010.0100.00.100", "000.0010.1000.00.100", "000.0111.1111.00.011"}},
        {104, true, 10, {"0010.0110.1001.11.111", "0110.1001.1001.11.100", "0010.1001.1001.00.100", "0010.1001.1111.00.110", "0010.1001.0001.00.100", "0010.1001.0001.00.100", "0111.0110.0001.00.100"}}
    };
    for (const auto& item : cases) {
        Frame expected = {};
        for (int y = 0; y < 7; ++y)
            for (size_t x = 0; x < strlen(item.rows[y]); ++x)
                if (item.rows[y][x] == '1') expected[y][item.x + x] = TEXT;
        same_region(render(item.value, item.fahrenheit, 800, 0), expected, 10, 31);
    }
    // Negative rounding must work identically on normal and frozen fetch frames.
    same_region(render(-11.6f, false, 600, 0), render(-12, false, 600, 0), 10, 31);
    same_region(render(-11.5f, false, 600, 99999), render(-12, false, 600, 0), 10, 31);
    same_region(render(-0.4f, true, 800, 0), render(0, true, 800, 0), 10, 31);
    assert(differs(render(7, true, 800, 0), render(7, false, 800, 0), 10, 31));
}
static void formatting_edges() {
    const struct { float input; const char* expected; } cases[] = {
        {7.49f, "7"}, {7.5f, "8"}, {-12.49f, "-12"}, {-12.5f, "-13"},
        {-0.49f, "0"}, {-0.5f, "-1"}, {104, "104"}, {-999, "-999"}, {9999, "9999"}
    };
    for (const auto& item : cases) {
        char out[8] = {};
        assert(weather_format_temperature(item.input, out, sizeof(out)));
        assert(strcmp(out, item.expected) == 0);
    }
    const float invalid[] = {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
        -999.5f, 9999.5f, -1000, 10000};
    const Frame unavailable = render(invalid[0], true, 800, 0);
    for (float value : invalid) {
        char out[8] = {};
        assert(!weather_format_temperature(value, out, sizeof(out)));
        assert(render(value, true, 200, UINT32_MAX) == unavailable);
    }
    char tiny[3] = {'a', 'b', 'c'};
    assert(!weather_format_temperature(104, tiny, sizeof(tiny)));
    assert(!weather_format_temperature(7, tiny, 0));
    assert(!weather_format_temperature(7, nullptr, 0));
    assert(!weather_format_temperature(7, nullptr, 8));
    for (float value : {-999.0f, -100.0f, 1000.0f, 9999.0f}) {
        const Frame valid = render(value, true, 800, 0);
        assert(differs(valid, unavailable, 10, 31));
        // The smaller fallback font must retain the complete rightmost unit.
        // F occupies x29..31, y1..5 for the four-character numeric values.
        assert(valid[1][29] == TEXT && valid[1][30] == TEXT && valid[1][31] == TEXT);
        assert(valid[2][29] == TEXT && valid[3][29] == TEXT);
        assert(valid[3][30] == TEXT && valid[4][29] == TEXT && valid[5][29] == TEXT);
        const Frame celsius = render(value, false, 800, 0);
        assert(celsius[1][30] == TEXT && celsius[1][31] == TEXT);
        assert(celsius[5][30] == TEXT && celsius[5][31] == TEXT);
    }
}
static void icons_and_motion() {
    const char* const sun[8] = {
        "........", "...#....", ".#...#..", "..###...",
        "#.###.#.", "..###...", ".#...#..", "...#...."
    };
    const char* const snow[8] = {
        "........", "...#....", ".#.#.#..", "..###...",
        ".#.#.#..", "...#....", "........", "........"
    };
    const Frame clear = render(7, true, 800, 0);
    golden_icon(clear, sun);
    const Frame flake = render(7, true, 600, 0);
    golden_icon(flake, snow);
    same_region(flake, render(7, true, 600, 649), 0, 7);
    const Frame falling = render(7, true, 600, 650);
    // Exact downward translation of every pixel, including the bright center.
    for (int y = 1; y < 8; ++y)
        for (int x = 0; x < 8; ++x) assert(falling[y][x] == flake[y - 1][x]);
    same_region(flake, render(7, true, 600, 14 * 650), 0, 7);
    for (uint32_t step = 0; step < 28; ++step) {
        const Frame frame = render(7, true, 600, step * 650);
        // A symmetric flake always remains centered at x=3. No sideways drift.
        for (int y = 0; y < 8; ++y) {
            assert(frame[y][0] == 0 && frame[y][6] == 0 && frame[y][7] == 0);
            assert(frame[y][1] == frame[y][5] && frame[y][2] == frame[y][4]);
        }
    }
    for (int condition : {801, 803}) {
        const Frame initial = render(7, true, condition, 0);
        same_region(initial, render(7, true, condition, 379), 0, 7);
        assert(differs(initial, render(7, true, condition, 380), 0, 7));
        same_region(initial, render(7, true, condition, 15 * 380), 0, 7);
    }
    // The overcast cloud shifts one pixel to the right before leaving the panel.
    const Frame cloud0 = render(7, true, 803, 0);
    const Frame cloud1 = render(7, true, 803, 380);
    for (int y = 0; y < 8; ++y)
        for (int x = 1; x < 8; ++x) assert(cloud1[y][x] == cloud0[y][x - 1]);
    const Frame rain = render(7, true, 500, 0);
    bool lanes[8] = {};
    for (uint32_t ms = 0; ms < 1620; ms += 270) {
        const Frame frame = render(7, true, 500, ms);
        for (int y = 4; y < 8; ++y)
            for (int x = 0; x < 8; ++x) lanes[x] = lanes[x] || frame[y][x] != 0;
    }
    int lane_count = 0;
    for (bool lane : lanes) lane_count += lane;
    assert(lane_count == 3);
    assert(differs(rain, render(7, true, 500, 270), 0, 7));
    for (int fog : {701, 721, 741}) assert(rain == render(7, true, fog, 0));
    const Frame lightning = render(7, true, 200, 0);
    const Frame dim_lightning = render(7, true, 200, 1000);
    // The approved bolt is centered at x=2,y=3 and remains recognizable when dim.
    const char* bolt[] = {"0011", "0110", "1111", "0010", "0100"};
    uint16_t bolt_color = 0, dim_color = 0;
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 4; ++x) {
        if (bolt[y][x] != '1') continue;
        const uint16_t a = lightning[y + 3][x + 2], b = dim_lightning[y + 3][x + 2];
        assert(a != 0 && b != 0);
        if (!bolt_color) { bolt_color = a; dim_color = b; }
        assert(a == bolt_color && b == dim_color);
    }
    assert(bolt_color != dim_color);
    assert((bolt_color >> 11) > (bolt_color & 31)); // Amber, not blue rain.
    same_region(lightning, dim_lightning, 10, 31);
    for (uint32_t ms : {0u, 1u, 270u, 650u, 3200u, 600000u, UINT32_MAX - 1, UINT32_MAX}) {
        assert(render(7, true, 800, ms) == clear);
        for (int fallback : {711, 731, 751, 761, 762, 771, 0, -1, 9999})
            assert(render(7, true, fallback, ms) == render(7, true, 711, 0));
    }
}
static void animation_boundaries() {
    for (int condition : {800, 801, 803, 300, 500, 511, 600, 200, 781, 711}) {
        const Frame baseline = render(-12, false, condition, 0);
        for (uint32_t ms = 0; ms < 18000; ms += 37) {
            const Frame current = render(-12, false, condition, ms);
            same_region(current, baseline, 10, 31);
            // Sampling intervening frames cannot change a timestamp's rendering.
            assert(current == render(-12, false, condition, ms));
        }
        for (uint32_t ms : {UINT32_MAX - 650, UINT32_MAX, 0u, 650u})
            same_region(render(-12, false, condition, ms), baseline, 10, 31);
    }
}
static void animation_clock_lifecycle() {
    WeatherAnimationState state = {};
    // A first render starts at zero regardless of the device's uptime.
    assert(weather_animation_elapsed(state, 600, 123456) == 0);
    assert(state.active && state.condition_id == 600);
    assert(state.epoch_ms == 123456 && state.last_render_ms == 123456);
    assert(weather_animation_elapsed(state, 600, 123456) == 0);
    assert(weather_animation_elapsed(state, 600, 123556) == 100);
    assert(weather_animation_elapsed(state, 600, 123756) == 300);
    assert(state.epoch_ms == 123456 && state.last_render_ms == 123756);

    // Exactly 500ms is continuous; a 501ms gap starts a fresh animation.
    assert(weather_animation_elapsed(state, 600, 124256) == 800);
    assert(weather_animation_elapsed(state, 600, 124757) == 0);
    assert(state.epoch_ms == 124757 && state.last_render_ms == 124757);
    assert(weather_animation_elapsed(state, 600, 124857) == 100);

    // A new condition restarts even when it shares the same visual category.
    assert(weather_animation_elapsed(state, 601, 124858) == 0);
    assert(state.condition_id == 601 && state.epoch_ms == 124858);
    assert(weather_animation_elapsed(state, 601, 124959) == 101);

    // Screen exit, pre-fetch and unavailable-data paths use this reset API.
    weather_animation_reset(state);
    assert(!state.active);
    weather_animation_reset(state); // Repeated resets remain harmless.
    assert(weather_animation_elapsed(state, 601, 124960) == 0);
    assert(weather_animation_elapsed(state, 601, 125010) == 50);

    WeatherAnimationState rollover = {};
    assert(weather_animation_elapsed(rollover, 803, UINT32_MAX - 100) == 0);
    // Across rollover, UINT32_MAX-100 -> 200 is 301ms, not a fetch gap.
    assert(weather_animation_elapsed(rollover, 803, 200) == 301);
    assert(rollover.epoch_ms == UINT32_MAX - 100 && rollover.last_render_ms == 200);
    assert(weather_animation_elapsed(rollover, 803, 700) == 801);
    assert(weather_animation_elapsed(rollover, 803, 1201) == 0);

    // A genuine long pause is still detected when it straddles rollover.
    weather_animation_reset(rollover);
    assert(weather_animation_elapsed(rollover, 803, UINT32_MAX - 100) == 0);
    assert(weather_animation_elapsed(rollover, 803, 401) == 0); // 502ms gap.
    assert(rollover.epoch_ms == 401 && rollover.last_render_ms == 401);

    // A recent frame keeps an old epoch valid: elapsed time also wraps safely.
    WeatherAnimationState long_running = {true, 600, UINT32_MAX - 1000, UINT32_MAX - 100};
    assert(weather_animation_elapsed(long_running, 600, 200) == 1201);
    assert(long_running.epoch_ms == UINT32_MAX - 1000);
}
int main() {
    mapping();
    golden_temperatures();
    formatting_edges();
    icons_and_motion();
    animation_boundaries();
    animation_clock_lifecycle();
    puts("Weather renderer: mapping, pixels, motion, text, numeric edges and clock lifecycle passed");
}
