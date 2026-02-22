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

// Get current date components
int time_get_day();       // 1-31
int time_get_month();     // 1-12
int time_get_weekday();   // 0=Sun, 1=Mon, ..., 6=Sat
const char* time_get_month_abbr();  // "JAN", "FEB", etc.

// Get formatted time string
void time_get_string(char* buf, int bufsize, bool use_24h);

// Get uptime in seconds
unsigned long time_get_uptime_sec();

#endif // TIME_ENGINE_H
