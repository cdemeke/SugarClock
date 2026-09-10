#include "display.h"
#include "hardware_pins.h"
#include "trend_arrows.h"

#include <FastLED.h>
#include <FastLED_NeoMatrix.h>
#include <Adafruit_GFX.h>

// LED array
static CRGB leds[MATRIX_NUM_LEDS];

// NeoMatrix instance
// Ulanzi TC001 uses row-major serpentine (zigzag) layout, top-left origin
static FastLED_NeoMatrix matrix(
    leds, MATRIX_WIDTH, MATRIX_HEIGHT,
    NEO_MATRIX_TOP + NEO_MATRIX_LEFT +
    NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG
);

static uint8_t current_brightness = 40;
static uint8_t transition_level = 255;
static bool composing_frame = false;
static bool frame_pending = false;

void display_init() {
    FastLED.addLeds<WS2812B, PIN_MATRIX_DATA, GRB>(leds, MATRIX_NUM_LEDS);
    // Keep FastLED's global brightness stable. Per-frame output scaling in
    // display_show() avoids rapid global brightness writes, which can produce
    // colored sparkle artifacts on some WS2812 matrices.
    FastLED.setBrightness(255);
    FastLED.setDither(DISABLE_DITHER);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000); // limit power draw

    matrix.begin();
    matrix.setTextWrap(false);
    display_clear();
    display_show();
}

void display_clear() {
    matrix.fillScreen(0);
}

void display_begin_frame() {
    composing_frame = true;
    frame_pending = false;
}

void display_end_frame() {
    composing_frame = false;
    if (frame_pending) {
        frame_pending = false;
        display_show();
    }
}

void display_show() {
    // Normal, OTA and pairing renderers share one framebuffer. Sending each
    // intermediate image makes the LEDs alternate even within a single loop.
    if (composing_frame) {frame_pending = true;return;}

    uint8_t output_brightness = (uint8_t)(((uint16_t)current_brightness *
        transition_level + 127) / 255);
    FastLED.show(output_brightness);
}

void display_set_brightness(uint8_t brightness) {
    current_brightness = brightness;
}

uint8_t display_get_brightness() {
    return current_brightness;
}

void display_set_transition_level(uint8_t level) {
    transition_level = level;
}

uint16_t display_color(uint8_t r, uint8_t g, uint8_t b) {
    return matrix.Color(r, g, b);
}

void display_draw_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
        matrix.drawPixel(x, y, color);
    }
}

void display_flash(uint8_t r, uint8_t g, uint8_t b) {
    matrix.fillScreen(matrix.Color(r, g, b));
    display_show();
}

void display_fill(uint8_t r, uint8_t g, uint8_t b) {
    matrix.fillScreen(matrix.Color(r, g, b));
    display_show();
}

void display_draw_text(const char* text, int x, int y, uint16_t color) {
    matrix.setTextColor(color);
    matrix.setCursor(x, y);
    matrix.print(text);
}

int display_text_width(const char* text) {
    if (!text) return 0;
    // Default Adafruit GFX 5x7 font advances 6px per glyph
    return (int)strlen(text) * 6;
}

void display_draw_text_scrolled(const char* text, int y, uint16_t color, int offset) {
    if (!text) return;
    matrix.setTextColor(color);
    matrix.setCursor(MATRIX_WIDTH - offset, y);
    matrix.print(text);
}

// Marquee state. A single shared marquee is enough: only one screen scrolls at
// a time, and switching strings restarts the scroll from the right edge.
static char scroll_text[96] = "";
static int scroll_offset = 0;
static unsigned long scroll_last_step_ms = 0;

void display_scroll_reset() {
    scroll_text[0] = '\0';
    scroll_offset = 0;
    scroll_last_step_ms = 0;
}

bool display_scroll_text(const char* text, int y, uint16_t color, unsigned int speed_ms) {
    if (!text) return false;
    if (strncmp(scroll_text, text, sizeof(scroll_text) - 1) != 0) {
        strncpy(scroll_text, text, sizeof(scroll_text) - 1);
        scroll_text[sizeof(scroll_text) - 1] = '\0';
        scroll_offset = 0;
        scroll_last_step_ms = millis();
    }

    int span = display_text_width(scroll_text) + MATRIX_WIDTH;
    bool cycled = false;

    unsigned long now = millis();
    if (speed_ms == 0) speed_ms = 1;
    while (now - scroll_last_step_ms >= speed_ms) {
        scroll_last_step_ms += speed_ms;
        scroll_offset++;
        if (scroll_offset >= span) {
            scroll_offset = 0;
            cycled = true;
        }
    }

    display_draw_text_scrolled(scroll_text, y, color, scroll_offset);
    return cycled;
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
