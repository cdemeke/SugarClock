#include "display.h"
#include "glucose_format.h"
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
static DisplayFrame frame_buffers[2];
static uint8_t published_index = 0;
static bool viewer_requested = false;
static bool frame_ready = false;
static uint32_t last_frame_request_ms = 0;
static uint32_t last_frame_publish_ms = 0;
static uint32_t frame_epoch = 1;
#ifdef ARDUINO_ARCH_ESP32
static portMUX_TYPE frame_mux = portMUX_INITIALIZER_UNLOCKED;
#endif

void display_copy_frame(DisplayFrame& frame) {
#ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&frame_mux);
#endif
    frame = frame_buffers[published_index];
#ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&frame_mux);
#endif
}

DisplayFrameStatus display_request_frame() {
    const uint32_t now = static_cast<uint32_t>(millis());
#ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&frame_mux);
#endif
    if (!viewer_requested || uint32_t(now - last_frame_request_ms) > 5000) frame_ready = false;
    viewer_requested = true;
    last_frame_request_ms = now;
    const DisplayFrameStatus status = {frame_ready, frame_buffers[published_index].sequence, frame_epoch};
#ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&frame_mux);
#endif
    return status;
}

void display_init() {
#ifdef ARDUINO_ARCH_ESP32
    frame_epoch = esp_random(); // ETags must not collide across device reboots.
#endif
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

void display_show() {
    uint8_t output_brightness = (uint8_t)(((uint16_t)current_brightness *
        transition_level + 127) / 255);
    FastLED.show(output_brightness);
    static_assert(MATRIX_WIDTH == 32 && MATRIX_HEIGHT == 8, "Update DisplayFrame dimensions");
    const uint32_t now = static_cast<uint32_t>(millis());
#ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&frame_mux);
#endif
    const bool capture = viewer_requested && uint32_t(now - last_frame_request_ms) <= 5000 &&
        (!frame_ready || uint32_t(now - last_frame_publish_ms) >= 250);
#ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&frame_mux);
#endif
    if (!capture) return;

    // Only the render task writes. Readers copy the published buffer under the
    // lock, while the renderer fills the other static buffer without clearing it.
    const uint8_t next_index = 1 - published_index;
    DisplayFrame& next = frame_buffers[next_index];
    for (int y = 0; y < MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < MATRIX_WIDTH; ++x) {
            const CRGB& pixel = leds[y * MATRIX_WIDTH + ((y & 1) ? MATRIX_WIDTH - 1 - x : x)];
            const int offset = (y * MATRIX_WIDTH + x) * 3;
            next.rgb[offset] = pixel.r;
            next.rgb[offset + 1] = pixel.g;
            next.rgb[offset + 2] = pixel.b;
        }
    }
    const bool changed = memcmp(next.rgb, frame_buffers[published_index].rgb, sizeof(next.rgb)) != 0;
#ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&frame_mux);
#endif
    next.sequence = frame_buffers[published_index].sequence + ((!frame_ready || changed) ? 1 : 0);
    published_index = next_index;
    frame_ready = true;
    last_frame_publish_ms = now;
#ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&frame_mux);
#endif
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

void display_draw_centered_text(const char* text, int y, uint16_t color) {
    int width = display_text_width(text) - 1;
    display_draw_text(text, (MATRIX_WIDTH - width) / 2, y, color);
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

int display_draw_glucose(int value, uint16_t color, bool use_mmol) {
    display_clear();

    char buf[8];
    format_glucose_value(buf, sizeof(buf), value, use_mmol);
    // Six pixels per character, plus six for the trend arrow. Both normal
    // and stale screens use this one layout calculation.
    int text_width = display_text_width(buf);
    int x = (MATRIX_WIDTH - text_width - 6) / 2;
    display_draw_text(buf, x, 0, color);
    return x + text_width + 1;
}

void display_draw_glucose_delta(int delta, int trend, uint16_t color, bool use_mmol) {
    char buf[8];
    format_glucose_delta(buf, sizeof(buf), delta, use_mmol);
    // Omit the trailing glyph spacing when fitting and centering, as on pets.
    int width = display_text_width(buf) - 1;
    bool show_arrow = width <= MATRIX_WIDTH - 8;
    if (show_arrow) {
        display_draw_trend(trend, 1, 0, color);
        display_draw_text(buf, 8, 0, color);
    } else {
        display_draw_centered_text(buf, 0, color);
    }
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
