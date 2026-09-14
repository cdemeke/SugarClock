#include "weather_render.h"
#include "display.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
// Row bitmasks, with the most significant of width bits at the left.
const uint8_t tall_digits[10][7] = {
    {6,9,9,9,9,9,6}, {2,6,2,2,2,2,7}, {6,9,1,2,4,8,15},
    {14,1,1,6,1,1,14}, {9,9,9,15,1,1,1}, {15,8,8,14,1,1,14},
    {6,8,8,14,9,9,6}, {15,1,2,2,4,4,4}, {6,9,9,6,9,9,6},
    {6,9,9,7,1,1,6}
};
const uint8_t small_digits[10][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {6,1,2,4,7}, {6,1,2,1,6},
    {5,5,7,1,1}, {7,4,6,1,6}, {3,4,7,5,7}, {7,1,2,2,2},
    {7,5,7,5,7}, {7,5,7,1,6}
};
const uint8_t tall_minus[] = {0,0,0,7,0,0,0};
const uint8_t small_minus[] = {0,0,7,0,0};
const uint8_t tall_f[] = {7,4,4,6,4,4,4};
const uint8_t tall_c[] = {3,4,4,4,4,4,3};
const uint8_t small_f[] = {7,4,6,4,4};
const uint8_t small_c[] = {3,4,4,4,3};
const uint8_t degree[] = {3,3};
const uint8_t cloud_rows[] = {24,60,126,63};
const uint8_t cloud_top[] = {24,36};
const uint8_t sun_rays[] = {8,34,0,65,0,34,8};
const uint8_t sun_center[] = {28,28,28};
const uint8_t snow_rows[] = {4,21,14,21,4};
const uint8_t bolt_rows[] = {3,6,15,2,4};
const uint8_t funnel_rows[] = {127,62,28,12,8,16};

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t opacity = 255) {
    return display_color(static_cast<uint16_t>(r) * opacity / 255,
                         static_cast<uint16_t>(g) * opacity / 255,
                         static_cast<uint16_t>(b) * opacity / 255);
}

void sprite(const uint8_t* rows, int width, int height, int x, int y,
            uint16_t color, bool icon = true) {
    for (int dy = 0; dy < height; ++dy) {
        for (int dx = 0; dx < width; ++dx) {
            const int px = x + dx;
            const int py = y + dy;
            if ((rows[dy] & (1 << (width - dx - 1))) && py >= 0 && py < 8 &&
                px >= (icon ? 0 : 10) && px < (icon ? 8 : 32)) {
                display_draw_pixel(px, py, color);
            }
        }
    }
}

void icon_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < 8 && y >= 0 && y < 8) display_draw_pixel(x, y, color);
}

void cloud(int x, int y) {
    sprite(cloud_rows, 7, 4, x, y, rgb(139,161,177));
    sprite(cloud_top, 7, 2, x, y, rgb(183,200,210));
}

void sun(bool partial) {
    const int y = partial ? 0 : 1;
    sprite(sun_rays, 7, 7, 0, y, rgb(239,180,86,204));
    sprite(sun_center, 7, 3, 0, y + 2, rgb(239,180,86));
}

void render_icon(WeatherVisual visual, uint32_t elapsed) {
    switch (visual) {
        case WeatherVisual::Clear:
            sun(false);
            break;
        case WeatherVisual::PartlyCloudy:
            sun(true);
            cloud(static_cast<int>((elapsed / 380 + 7) % 15) - 7, 3);
            break;
        case WeatherVisual::Cloudy:
            cloud(static_cast<int>((elapsed / 380 + 7) % 15) - 7, 2);
            break;
        case WeatherVisual::CloudFallback:
            cloud(0, 2);
            break;
        case WeatherVisual::Snow:
            for (uint32_t i = 0; i < 2; ++i) {
                const int y = static_cast<int>((elapsed / 650 + i * 7 + 6) % 14) - 5;
                sprite(snow_rows, 5, 5, 1, y, rgb(196,212,237,230));
                icon_pixel(3, y + 2, rgb(237,244,255));
            }
            break;
        case WeatherVisual::Tornado:
            sprite(funnel_rows, 7, 6, (elapsed / 850) % 2, 1,
                   rgb(139,161,177,204));
            break;
        case WeatherVisual::Rain:
        case WeatherVisual::Drizzle:
        case WeatherVisual::Sleet:
        case WeatherVisual::Storm: {
            cloud(0, 0);
            const bool drizzle = visual == WeatherVisual::Drizzle;
            const bool storm = visual == WeatherVisual::Storm;
            const int lanes = visual == WeatherVisual::Rain ? 3 : 2;
            const uint32_t period = drizzle ? 400 : 270;
            for (int i = 0; i < lanes; ++i) {
                // Exact 2.4-step lane offset from the design, without floats or
                // multiplying elapsed time (which would overflow on long runs).
                const uint32_t step = elapsed / period +
                    ((elapsed % period) * 5 + i * 12 * period) / (5 * period);
                const int y = 4 + step % 6;
                const bool icy = visual == WeatherVisual::Sleet && i == 1;
                const int x = storm ? (i == 0 ? 0 : 7) :
                    1 + i * (visual == WeatherVisual::Rain ? 2 : 3) +
                    (icy && (elapsed / 950 + i) % 2 == 1 ? 1 : 0);
                icon_pixel(x, y, icy ? rgb(196,212,237,204) : rgb(99,155,234,204));
                if (!icy && !drizzle && y - 1 >= 4)
                    icon_pixel(x, y - 1, rgb(99,155,234,87));
            }
            if (storm) {
                sprite(bolt_rows, 4, 5, 2, 3,
                       rgb(255,206,104,elapsed % 3200 < 850 ? 255 : 158));
            }
            break;
        }
    }
}

int numeric_width(const char* digits, bool small) {
    int width = 0;
    for (const char* p = digits; *p; ++p) width += (*p == '-' || small) ? 4 : 5;
    return width;
}

void render_temperature(const char* digits, bool available, bool use_f,
                        uint16_t color) {
    // Numeric width includes its final gap. Degree + gap + unit add six pixels.
    const bool small = numeric_width(digits, false) + (available ? 6 : 3) > 22;
    const int width = numeric_width(digits, small) + (available ? 6 : 3);
    int x = 10 + (22 - width) / 2;
    const int y = small ? 1 : 0;
    const int height = small ? 5 : 7;
    for (const char* p = digits; *p; ++p) {
        const bool minus = *p == '-';
        const uint8_t* rows = minus ? (small ? small_minus : tall_minus) :
            (small ? small_digits[*p - '0'] : tall_digits[*p - '0']);
        const int glyph_width = minus || small ? 3 : 4;
        sprite(rows, glyph_width, height, x, y, color, false);
        x += glyph_width + 1;
    }
    if (available) {
        sprite(degree, 2, 2, x, y, color, false);
        x += 3;
    }
    const uint8_t* unit = use_f ? (small ? small_f : tall_f) :
                                  (small ? small_c : tall_c);
    sprite(unit, 3, height, x, y, color, false);
}
} // namespace

uint32_t weather_animation_elapsed(WeatherAnimationState& state,
                                   int condition_id, uint32_t now) {
    if (!state.active || state.condition_id != condition_id ||
        static_cast<uint32_t>(now - state.last_render_ms) > 500) {
        state.epoch_ms = now;
        state.condition_id = condition_id;
        state.active = true;
    }
    state.last_render_ms = now;
    return static_cast<uint32_t>(now - state.epoch_ms);
}

void weather_animation_reset(WeatherAnimationState& state) {
    state.active = false;
}

WeatherVisual weather_visual_for_condition(int condition_id) {
    if (condition_id >= 200 && condition_id < 300) return WeatherVisual::Storm;
    if (condition_id >= 300 && condition_id < 400) return WeatherVisual::Drizzle;
    if (condition_id == 511 || condition_id == 611 || condition_id == 612 ||
        condition_id == 613 || condition_id == 615 || condition_id == 616)
        return WeatherVisual::Sleet;
    if (condition_id >= 500 && condition_id < 600) return WeatherVisual::Rain;
    if (condition_id >= 600 && condition_id < 700) return WeatherVisual::Snow;
    if (condition_id == 701 || condition_id == 721 || condition_id == 741)
        return WeatherVisual::Rain;
    if (condition_id == 800) return WeatherVisual::Clear;
    if (condition_id == 801 || condition_id == 802) return WeatherVisual::PartlyCloudy;
    if (condition_id == 803 || condition_id == 804) return WeatherVisual::Cloudy;
    if (condition_id == 781) return WeatherVisual::Tornado;
    return WeatherVisual::CloudFallback;
}

bool weather_format_temperature(float temp, char* out, size_t size) {
    if (!out || size == 0) return false;
    out[0] = '\0';
    if (!isfinite(temp)) return false;
    const float rounded = roundf(temp);
    if (rounded < -999.0f || rounded > 9999.0f) return false;
    const int written = snprintf(out, size, "%d", static_cast<int>(rounded));
    if (written < 0 || static_cast<size_t>(written) >= size) {
        out[0] = '\0';
        return false;
    }
    return true;
}

void weather_render(float temp, bool use_f, int condition_id,
                    uint32_t elapsed_ms, uint16_t text_color) {
    char digits[6];
    const bool available = weather_format_temperature(temp, digits, sizeof(digits));
    if (available) {
        render_icon(weather_visual_for_condition(condition_id), elapsed_ms);
    } else {
        strcpy(digits, "--");
        const uint8_t dash[] = {15};
        sprite(dash, 4, 1, 2, 3, rgb(103,114,123));
    }
    render_temperature(digits, available, use_f, text_color);
}
