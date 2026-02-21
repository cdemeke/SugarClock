#ifndef TREND_ARROWS_H
#define TREND_ARROWS_H

#include <stdint.h>

// Trend arrow types
enum TrendType {
    TREND_RISING_FAST  = 0,
    TREND_RISING       = 1,
    TREND_FLAT         = 2,
    TREND_FALLING      = 3,
    TREND_FALLING_FAST = 4,
    TREND_UNKNOWN      = 5
};

// 5x7 pixel-art bitmaps for trend arrows
// Each row is a byte, LSB = leftmost pixel
// Designed to be visually distinct at arm's length on 8-row matrix

// Rising Fast (double up arrow)
//  .X.X.
//  XXXXX
//  .X.X.
//  .X.X.
//  .X.X.
//  .X.X.
//  .X.X.
static const uint8_t TREND_BITMAP_RISING_FAST[7] = {
    0b01010,  // .X.X.
    0b11111,  // XXXXX
    0b01010,  // .X.X.
    0b01010,  // .X.X.
    0b01010,  // .X.X.
    0b01010,  // .X.X.
    0b01010,  // .X.X.
};

// Rising (up arrow)
//  ..X..
//  .XXX.
//  X.X.X
//  ..X..
//  ..X..
//  ..X..
//  ..X..
static const uint8_t TREND_BITMAP_RISING[7] = {
    0b00100,  // ..X..
    0b01110,  // .XXX.
    0b10101,  // X.X.X
    0b00100,  // ..X..
    0b00100,  // ..X..
    0b00100,  // ..X..
    0b00100,  // ..X..
};

// Flat (right arrow)
//  .....
//  ..X..
//  ...X.
//  XXXXX
//  ...X.
//  ..X..
//  .....
static const uint8_t TREND_BITMAP_FLAT[7] = {
    0b00000,  // .....
    0b00100,  // ..X..
    0b00010,  // ...X.
    0b11111,  // XXXXX
    0b00010,  // ...X.
    0b00100,  // ..X..
    0b00000,  // .....
};

// Falling (down arrow)
//  ..X..
//  ..X..
//  ..X..
//  ..X..
//  X.X.X
//  .XXX.
//  ..X..
static const uint8_t TREND_BITMAP_FALLING[7] = {
    0b00100,  // ..X..
    0b00100,  // ..X..
    0b00100,  // ..X..
    0b00100,  // ..X..
    0b10101,  // X.X.X
    0b01110,  // .XXX.
    0b00100,  // ..X..
};

// Falling Fast (double down arrow)
//  .X.X.
//  .X.X.
//  .X.X.
//  .X.X.
//  .X.X.
//  XXXXX
//  .X.X.
static const uint8_t TREND_BITMAP_FALLING_FAST[7] = {
    0b01010,  // .X.X.
    0b01010,  // .X.X.
    0b01010,  // .X.X.
    0b01010,  // .X.X.
    0b01010,  // .X.X.
    0b11111,  // XXXXX
    0b01010,  // .X.X.
};

// Array of pointers for indexed access
static const uint8_t* TREND_BITMAPS[] = {
    TREND_BITMAP_RISING_FAST,
    TREND_BITMAP_RISING,
    TREND_BITMAP_FLAT,
    TREND_BITMAP_FALLING,
    TREND_BITMAP_FALLING_FAST
};

// Trend name strings
static const char* TREND_NAMES[] = {
    "RisingFast",
    "Rising",
    "Flat",
    "Falling",
    "FallingFast",
    "Unknown"
};

#endif // TREND_ARROWS_H
