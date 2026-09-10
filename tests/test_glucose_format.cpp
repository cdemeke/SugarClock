#include "display.h"
#include "FastLED_NeoMatrix.h"
#include "glucose_format.h"
#include "hardware_pins.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <climits>
#include <initializer_list>

FakeLEDs FastLED;
unsigned long millis() { return 0; }

// Compile the real stale-screen case, with only its inputs replaced by fixtures.
struct GlucoseReading { int glucose, trend; bool valid; };
struct Config { bool use_mmol; uint32_t color_stale; } cfg;
GlucoseReading reading_fixture;
const GlucoseReading& http_get_reading() { return reading_fixture; }
uint8_t effective_brightness() { return 40; }
uint16_t color_from_uint32(uint32_t color) { return color; }
constexpr int STATE_STALE_WARNING = 0, TREND_UNKNOWN = 5;
void render_stale() {
    switch (STATE_STALE_WARNING) {
#include "stale_screen.inc"
    }
}

void assert_text(const char* expected, int x) {
    const auto& glyphs = drawing().glyphs;
    assert(glyphs.size() == std::strlen(expected));
    for (size_t i = 0; i < glyphs.size(); ++i) {
        const auto& glyph = glyphs[i];
        assert(glyph.value == expected[i]);
        assert(glyph.x == x + static_cast<int>(i) * 6);
        assert(glyph.x >= 0 && glyph.x + 4 < MATRIX_WIDTH);
        assert(glyph.y == 0 && glyph.y + 6 < MATRIX_HEIGHT);
    }
    // Every real trend bitmap pixel must be on-screen and outside glyph bounds.
    for (const auto& pixel : drawing().pixels) {
        assert(pixel.x >= 0 && pixel.x < MATRIX_WIDTH);
        assert(pixel.y >= 0 && pixel.y < MATRIX_HEIGHT);
        for (const auto& glyph : glyphs) {
            assert(pixel.x < glyph.x || pixel.x > glyph.x + 4 ||
                   pixel.y < glyph.y || pixel.y > glyph.y + 6);
        }
    }
}

int main() {
    display_init();
    // Both units, every supported reading, and all five actual trend bitmaps.
    // The six-pixel glyph advance exposes overflow and arrow collisions.
    for (bool mmol : {false, true, false}) {
        for (int mgdl = 1; mgdl <= 600; ++mgdl) {
            char expected[8];
            format_glucose_value(expected, sizeof(expected), mgdl, mmol);
            int x = (MATRIX_WIDTH - static_cast<int>(std::strlen(expected)) * 6 - 6) / 2;
            for (int trend = 0; trend < 5; ++trend) {
                int arrow_x = display_draw_glucose(mgdl, 1, mmol);
                assert(arrow_x > x + static_cast<int>(std::strlen(expected) - 1) * 6 + 4);
                assert(arrow_x + 4 < MATRIX_WIDTH);
                display_draw_trend(trend, arrow_x, 0, 1);
                assert_text(expected, x);
                assert(!drawing().pixels.empty());

                cfg = {mmol, 1};
                reading_fixture = {mgdl, trend, true};
                render_stale();
                assert_text(expected, x);
                assert(!drawing().pixels.empty());
            }
        }
    }
    // Invalid stale data remains a placeholder; unknown trends have no arrow.
    reading_fixture = {180, TREND_UNKNOWN, true};
    render_stale();
    assert_text("180", 4);
    assert(drawing().pixels.empty());
    reading_fixture.valid = false;
    render_stale();
    assert_text("---", 7);

    // Four glyphs have 24px advance (23px ink), exactly the old arrow boundary.
    // A fifth glyph uses the full width and centers without trailing spacing.
    for (bool mmol : {false, true}) {
        for (int sign : {-1, 1}) {
            display_clear();
            display_draw_glucose_delta(sign * (mmol ? 18 : 100), 2, 1, mmol);
            assert_text(mmol ? (sign < 0 ? "-1.0" : "+1.0") :
                              (sign < 0 ? "-100" : "+100"), 8);
            assert(!drawing().pixels.empty());
            display_clear();
            display_draw_glucose_delta(sign * (mmol ? 180 : 1000), 2, 1, mmol);
            assert_text(mmol ? (sign < 0 ? "-10.0" : "+10.0") :
                              (sign < 0 ? "-1000" : "+1000"), 1);
            assert(drawing().pixels.empty());
        }
    }
    // Compare formatted readings and signed deltas against independent rounding.
    for (int mgdl = -600; mgdl <= 600; ++mgdl) {
        char formatted[8], expected[16];
        format_glucose_delta(formatted, sizeof(formatted), mgdl, true);
        snprintf(expected, sizeof(expected), "%+.1f", std::round((mgdl / 18.0) * 10) / 10);
        assert(std::strcmp(formatted, expected) == 0);
        format_glucose_value(formatted, sizeof(formatted), mgdl, true);
        snprintf(expected, sizeof(expected), "%.1f", std::round((mgdl / 18.0) * 10) / 10);
        assert(std::strcmp(formatted, expected) == 0);
        format_glucose_delta(formatted, sizeof(formatted), mgdl, false);
        snprintf(expected, sizeof(expected), "%+d", mgdl);
        assert(std::strcmp(formatted, expected) == 0);
    }
    for (int mgdl : {INT_MIN, INT_MAX}) {
        struct { char text[8]; char sentinel; } small = {{}, 'X'};
        format_glucose_delta(small.text, sizeof(small.text), mgdl, true);
        assert(small.sentinel == 'X' && small.text[7] == '\0');
    }
}
