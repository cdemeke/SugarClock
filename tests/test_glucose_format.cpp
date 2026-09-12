#include "display.h"
#include "FastLED_NeoMatrix.h"
#include "glucose_format.h"
#include "glucose_render.h"
#include "hardware_pins.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <climits>
#include <initializer_list>

FakeLEDs FastLED;
static uint32_t fake_ms = 0;
unsigned long millis() { return fake_ms; }

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
    // No subscriber: drawing the real output must not publish a browser frame.
    DisplayFrame initial;
    display_copy_frame(initial);
    display_draw_pixel(3, 1, display_color(255, 0, 0));
    display_draw_pixel(1, 0, display_color(0, 255, 0));
    display_draw_pixel(31, 7, display_color(0, 0, 255));
    assert(FastLED.pixels[60].r == 255); // Hardware address of logical (3, 1).
    display_set_brightness(10);
    display_show();
    DisplayFrame published;
    display_copy_frame(published);
    assert(published.sequence == initial.sequence);
    assert(!display_request_frame().ready);
    display_show();
    display_copy_frame(published);
    assert(published.sequence == initial.sequence + 1);
    // Assert logical coordinates, not the inverse-mapping implementation.
    for (int pixel = 0; pixel < 256; ++pixel) {
        assert(published.rgb[pixel * 3] == (pixel == 35 ? 255 : 0));
        assert(published.rgb[pixel * 3 + 1] == (pixel == 1 ? 255 : 0));
        assert(published.rgb[pixel * 3 + 2] == (pixel == 255 ? 255 : 0));
    }
    fake_ms = 250;
    display_show(); // Same pixels: conditional sequence must stay unchanged.
    assert(display_request_frame().sequence == published.sequence);
    display_clear();
    DisplayFrame not_yet_shown;
    display_copy_frame(not_yet_shown);
    assert(std::memcmp(published.rgb, not_yet_shown.rgb, sizeof(published.rgb)) == 0);
    assert(published.sequence == not_yet_shown.sequence);
    fake_ms = 499;
    display_show(); // Rate limited even though pixels changed.
    display_copy_frame(not_yet_shown);
    assert(not_yet_shown.sequence == published.sequence);
    fake_ms = 500;
    display_show();
    display_copy_frame(not_yet_shown);
    for (uint8_t channel : not_yet_shown.rgb) assert(channel == 0);
    assert(not_yet_shown.sequence == published.sequence + 1);
    fake_ms = 5251; // Last request was at 250: the five-second lease expired.
    display_draw_pixel(3, 1, display_color(255, 0, 0));
    display_show();
    display_copy_frame(published);
    assert(published.sequence == not_yet_shown.sequence);
    assert(!display_request_frame().ready); // Do not serve the stale idle frame.
    display_show();
    assert(display_request_frame().ready);
    assert(display_request_frame().sequence == published.sequence + 1);
    fake_ms = UINT32_MAX - 10;
    assert(!display_request_frame().ready);
    display_show();
    fake_ms = 15; // Lease remains valid across millis() wraparound.
    assert(display_request_frame().ready);
    fake_ms = 6000; // Disable capture for the remaining rendering tests.
    display_clear();

    AppConfig cfg = {};
    GlucoseReading reading_fixture = {};
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

                cfg.use_mmol = mmol;
                cfg.color_stale = 0x808080;
                reading_fixture.glucose = mgdl;
                reading_fixture.trend = static_cast<TrendType>(trend);
                reading_fixture.valid = true;
                const int shows_before = FastLED.shows;
                glucose_render_stale(reading_fixture, cfg, 37);
                assert(FastLED.shows == shows_before + 1 && FastLED.brightness == 37);
                assert_text(expected, x);
                assert(!drawing().pixels.empty());
            }
        }
    }
    // Invalid stale data remains a placeholder; unknown trends have no arrow.
    reading_fixture.glucose = 180;
    reading_fixture.trend = TREND_UNKNOWN;
    reading_fixture.valid = true;
    glucose_render_stale(reading_fixture, cfg, 37);
    assert_text("180", 4);
    assert(drawing().pixels.empty());
    reading_fixture.valid = false;
    glucose_render_stale(reading_fixture, cfg, 37);
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
    // The linked delta-flash renderer clears old content, uses visible bounds,
    // emits one frame, and matches the centered trend page for wide deltas.
    for (bool mmol : {false, true}) {
        for (int delta : {-600, -180, -18, -1, 0, 1, 18, 180, 600}) {
            char expected[8];
            format_glucose_delta(expected, sizeof(expected), delta, mmol);
            const int width = static_cast<int>(std::strlen(expected)) * 6 - 1;
            const int x = (MATRIX_WIDTH - width) / 2;
            display_draw_text("OLD", 0, 0, 1);
            display_draw_trend(TREND_FLAT, 1, 0, 1);
            const int shows_before = FastLED.shows;
            glucose_render_delta_flash(delta, 1, mmol);
            assert(FastLED.shows == shows_before + 1);
            assert_text(expected, x);
            assert(drawing().pixels.empty());
            if (width > MATRIX_WIDTH - 8) {
                display_clear();
                display_draw_glucose_delta(delta, TREND_FLAT, 1, mmol);
                assert_text(expected, x);
                assert(drawing().pixels.empty());
            }
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
