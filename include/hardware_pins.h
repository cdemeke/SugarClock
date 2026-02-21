#ifndef HARDWARE_PINS_H
#define HARDWARE_PINS_H

// ============================================
// Ulanzi TC001 GPIO Pin Definitions
// ESP32-WROOM-32D
// ============================================

// WS2812B RGB LED Matrix (8x32 = 256 LEDs)
#define PIN_MATRIX_DATA     32

// Matrix dimensions
#define MATRIX_WIDTH        32
#define MATRIX_HEIGHT       8
#define MATRIX_NUM_LEDS     (MATRIX_WIDTH * MATRIX_HEIGHT)

// Buttons (active LOW with internal pull-up)
#define PIN_BUTTON_LEFT     26
#define PIN_BUTTON_MIDDLE   27
#define PIN_BUTTON_RIGHT    14

// Buzzer
#define PIN_BUZZER          15

// Light Dependent Resistor (analog input)
#define PIN_LDR             35

// Battery voltage (analog input)
#define PIN_BATTERY         34

// I2C (for DS1307 RTC)
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22

#endif // HARDWARE_PINS_H
