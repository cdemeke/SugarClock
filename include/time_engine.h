#ifndef TIME_ENGINE_H
#define TIME_ENGINE_H

#include <stdint.h>

// Initialize time engine (NTP + RTC)
void time_init();

// Time loop - handles NTP resync
void time_loop();

// Check if time is available from any source
bool time_is_available();

// Get current time components
int time_get_hour();    // 0-23
int time_get_minute();  // 0-59
int time_get_second();  // 0-59

// Get formatted time string
void time_get_string(char* buf, int bufsize, bool use_24h);

// Get uptime in seconds
unsigned long time_get_uptime_sec();

#endif // TIME_ENGINE_H
