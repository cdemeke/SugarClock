#include "display.h"
#include "hardware_pins.h"
#include "trend_arrows.h"

#include <FastLED.h>
#include <FastLED_NeoMatrix.h>
#include <Adafruit_GFX.h>

// LED array
static CRGB leds[MATRIX_NUM_LEDS];

// NeoMatrix instance
// TC001 uses row-major serpentine (zigzag) layout, top-left origin
static FastLED_NeoMatrix matrix(
    leds, MATRIX_WIDTH, MATRIX_HEIGHT,
    NEO_MATRIX_TOP + NEO_MATRIX_LEFT +
    NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG
);

static uint8_t current_brightness = 40;

void display_init() {
    FastLED.addLeds<WS2812B, PIN_MATRIX_DATA, GRB>(leds, MATRIX_NUM_LEDS);
    FastLED.setBrightness(current_brightness);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000); // limit power draw

    matrix.begin();
    matrix.setTextWrap(false);
    matrix.setBrightness(current_brightness);

    display_clear();
    display_show();
}

void display_clear() {
    matrix.fillScreen(0);
}

void display_show() {
    matrix.show();
}

void display_set_brightness(uint8_t brightness) {
    current_brightness = brightness;
    matrix.setBrightness(brightness);
    FastLED.setBrightness(brightness);
}

uint8_t display_get_brightness() {
    return current_brightness;
}

uint16_t display_color(uint8_t r, uint8_t g, uint8_t b) {
    return matrix.Color(r, g, b);
}

void display_fill(uint8_t r, uint8_t g, uint8_t b) {
    matrix.fillScreen(matrix.Color(r, g, b));
    matrix.show();
}

void display_draw_text(const char* text, int x, int y, uint16_t color) {
    matrix.setTextColor(color);
    matrix.setCursor(x, y);
    matrix.print(text);
}

void display_draw_glucose(int value, uint16_t color) {
    display_clear();

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    int len = strlen(buf);

    // Each character in default 5x7 font is 6px wide (5 + 1 spacing)
    // Calculate total width of glucose text
    int text_width = len * 6;

    // Leave room for trend arrow (6px) on the right
    // Center the glucose + arrow combination
    int total_width = text_width + 6; // 6px for arrow area
    int x = (MATRIX_WIDTH - total_width) / 2;
    int y = 0; // top-aligned for 5x7 font on 8-row matrix

    matrix.setTextColor(color);
    matrix.setCursor(x, y);
    matrix.print(buf);
}

void display_draw_trend(int trend, int x, int y, uint16_t color) {
    if (trend < 0 || trend > 4) return;

    const uint8_t* bitmap = TREND_BITMAPS[trend];
    for (int row = 0; row < 7; row++) {
        uint8_t rowData = bitmap[row];
        for (int col = 0; col < 5; col++) {
            if (rowData & (1 << (4 - col))) {
                matrix.drawPixel(x + col, y + row, color);
            }
        }
    }
}

void display_draw_bar(int value, int max_val, uint16_t color) {
    if (max_val <= 0) max_val = 100;
    int fill = (value * MATRIX_WIDTH) / max_val;
    if (fill > MATRIX_WIDTH) fill = MATRIX_WIDTH;
    if (fill < 0) fill = 0;

    // Draw bar on bottom 3 rows (rows 5, 6, 7)
    for (int x = 0; x < fill; x++) {
        for (int y = 5; y < 8; y++) {
            matrix.drawPixel(x, y, color);
        }
    }
    // Draw dim outline for remaining
    uint16_t dim = matrix.Color(30, 30, 30);
    for (int x = fill; x < MATRIX_WIDTH; x++) {
        matrix.drawPixel(x, 5, dim);
        matrix.drawPixel(x, 7, dim);
    }
}

void display_draw_time(int hour, int minute, bool show_colon, bool use_24h, uint16_t color) {
    display_clear();

    char buf[8];
    int display_hour = hour;

    if (!use_24h) {
        display_hour = hour % 12;
        if (display_hour == 0) display_hour = 12;
    }

    if (show_colon) {
        snprintf(buf, sizeof(buf), "%d:%02d", display_hour, minute);
    } else {
        snprintf(buf, sizeof(buf), "%d %02d", display_hour, minute);
    }

    int len = strlen(buf);
    int text_width = len * 6;
    int x = (MATRIX_WIDTH - text_width) / 2;

    matrix.setTextColor(color);
    matrix.setCursor(x, 0);
    matrix.print(buf);
}
