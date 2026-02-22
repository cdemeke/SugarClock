#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

// Initialize the 8x32 WS2812B matrix
void display_init();

// Clear all pixels
void display_clear();

// Push buffer to LEDs
void display_show();

// Set brightness (0-255)
void display_set_brightness(uint8_t brightness);

// Get current brightness
uint8_t display_get_brightness();

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

// Convert RGB to 16-bit color for GFX
uint16_t display_color(uint8_t r, uint8_t g, uint8_t b);

#endif // DISPLAY_H
