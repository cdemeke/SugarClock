#pragma once
#include "FastLED.h"
#include <vector>

constexpr int NEO_MATRIX_TOP = 0, NEO_MATRIX_LEFT = 0;
constexpr int NEO_MATRIX_ROWS = 0, NEO_MATRIX_ZIGZAG = 0;
struct Glyph { int x, y; char value; };
struct Pixel { int x, y; };
struct Drawing {
    std::vector<Glyph> glyphs;
    std::vector<Pixel> pixels;
};
inline Drawing& drawing() {
    static Drawing result;
    return result;
}

struct FastLED_NeoMatrix {
    CRGB* pixels;
    int width, height, cursor_x = 0, cursor_y = 0;
    FastLED_NeoMatrix(CRGB* p, int w, int h, int) : pixels(p), width(w), height(h) {}
    void begin() {}
    void setTextWrap(bool) {}
    void fillScreen(int) {
        for (int i = 0; i < width * height; ++i) pixels[i] = {};
        drawing() = {};
    }
    uint16_t Color(uint8_t r, uint8_t g, uint8_t b) { return (r << 8) | (g << 3) | b; }
    void setTextColor(uint16_t) {}
    void setCursor(int x, int y) { cursor_x = x; cursor_y = y; }
    void print(const char* s) {
        while (*s) {
            // Record the un-clipped 5x7 glyph bounds and real 6px advance.
            // This checks layout, not the font's individual lit pixels.
            drawing().glyphs.push_back({cursor_x, cursor_y, *s++});
            cursor_x += 6;
        }
    }
    void drawPixel(int x, int y, uint16_t color) {
        drawing().pixels.push_back({x, y});
        if (x >= 0 && x < width && y >= 0 && y < height) pixels[y * width + x].r = color;
    }
};
