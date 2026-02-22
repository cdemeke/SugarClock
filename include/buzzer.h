#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

// Initialize buzzer hardware (LEDC PWM on PIN_BUZZER)
void buzzer_init();

// Queue a beep pattern: count beeps at given frequency for duration_ms each
void buzzer_beep(int count, int freq = 2000, int duration_ms = 200);

// Non-blocking buzzer update - call every loop iteration
void buzzer_loop();

// Stop any active beeping immediately
void buzzer_stop();

// Check if buzzer is currently beeping
bool buzzer_is_active();

#endif // BUZZER_H
