#include "glucose_render.h"
#include "display.h"
#include "glucose_format.h"

void glucose_render_stale(const GlucoseReading& reading, const AppConfig& config,
                          uint8_t brightness) {
    display_set_brightness(brightness);
    display_clear();
    const uint32_t packed = config.color_stale;
    const uint16_t color = display_color((packed >> 16) & 0xFF,
                                        (packed >> 8) & 0xFF, packed & 0xFF);
    if (!reading.valid) {
        display_draw_text("---", 7, 0, color);
    } else {
        const int arrow_x = display_draw_glucose(reading.glucose, color, config.use_mmol);
        if (reading.trend != TREND_UNKNOWN) {
            display_draw_trend(reading.trend, arrow_x, 0, color);
        }
    }
    display_show();
}

void glucose_render_delta_flash(int delta, uint16_t color, bool use_mmol) {
    display_clear();
    char text[8];
    format_glucose_delta(text, sizeof(text), delta, use_mmol);
    display_draw_centered_text(text, 0, color);
    display_show();
}
