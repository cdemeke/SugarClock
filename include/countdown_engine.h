#ifndef COUNTDOWN_ENGINE_H
#define COUNTDOWN_ENGINE_H

#include <stdint.h>

// Initialize countdown engine
void countdown_init();

// Countdown loop (lightweight - just uses time)
void countdown_loop();

// Get remaining seconds until target (negative if past)
long countdown_get_remaining_sec();

// Check if countdown is configured with a valid target
bool countdown_is_configured();

#endif // COUNTDOWN_ENGINE_H
