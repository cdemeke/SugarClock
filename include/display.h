#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

// Initialize the 8x32 WS2812B matrix
void display_init();

// Clear all pixels
void display_clear();

// Compose competing renderers into one LED update. Boot calls remain immediate.
void display_begin_frame();
void display_end_frame();

// Push buffer to LEDs (deferred inside a composed frame)
void display_show();

// Set brightness (0-255)
void display_set_brightness(uint8_t brightness);

// Get current brightness
uint8_t display_get_brightness();

// Set per-frame transition opacity without changing the configured brightness.
// 0 is fully dark and 255 is fully visible.
void display_set_transition_level(uint8_t level);

// Draw glucose value centered on matrix with specified color
// color is a 16-bit RGB565 color for GFX compatibility
void display_draw_glucose(int value, uint16_t color);

// Draw general text at position
void display_draw_text(const char* text, int x, int y, uint16_t color);

// Draw a trend arrow at the specified position
// trend: 0=rising_fast, 1=rising, 2=flat, 3=falling, 4=falling_fast
void display_draw_trend(int trend, int x, int y, uint16_t color);

// Draw time display centered on matrix
void display_draw_time(int hour, int minute, bool show_colon, bool use_24h, uint16_t color);

// Draw a horizontal bar graph (bottom 3 rows of display)
// value/max determines fill width across 32 pixels
void display_draw_bar(int value, int max_val, uint16_t color);

// Draw a single pixel at (x, y) with a 16-bit color
void display_draw_pixel(int x, int y, uint16_t color);

// Flash the entire matrix with a solid color (non-blocking, caller manages timing)
void display_flash(uint8_t r, uint8_t g, uint8_t b);

// Fill entire matrix with a single color (for testing)
void display_fill(uint8_t r, uint8_t g, uint8_t b);

// Width in pixels that `text` occupies in the default 5x7 font (6px per glyph)
int display_text_width(const char* text);

// Draw `text` horizontally scrolled by `offset` pixels from the right edge.
// Caller owns the offset, so this stays a pure drawing call.
void display_draw_text_scrolled(const char* text, int y, uint16_t color, int offset);

// Self-timed marquee. Call once per render pass with the same `text`; the
// offset advances every `speed_ms` and resets when the string changes.
// Returns true on the frame where a full cycle has just completed, which lets
// callers page between several strings.
bool display_scroll_text(const char* text, int y, uint16_t color, unsigned int speed_ms);

// Reset the marquee so the next display_scroll_text() starts from the right edge
void display_scroll_reset();

// Convert RGB to 16-bit color for GFX
uint16_t display_color(uint8_t r, uint8_t g, uint8_t b);

#endif // DISPLAY_H
