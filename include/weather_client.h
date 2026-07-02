#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

#include <stddef.h>

struct WeatherReading {
    float temp;
    char description[32];
    int humidity;
    int condition_id;
    unsigned long received_at_ms;
    bool valid;
};

// Initialize weather client
void weather_init();

// Run one polling tick: executes any pending forced fetch, then a scheduled
// poll if due. Fetches block for seconds, so this must only be called from
// the background network task (net_task.cpp).
void weather_poll_tick();

// Get the latest weather reading (thread-safe copy)
WeatherReading weather_get_reading();

// Check if weather data has been received
bool weather_has_data();

// Request an immediate fetch on the network task and wait up to timeout_ms
// for it to finish. Returns true if it completed successfully; false on
// failure or timeout (a timed-out fetch keeps running and its result is
// published normally).
bool weather_force_fetch(unsigned long timeout_ms);

// Get the last HTTP status code from weather fetch
int weather_get_last_http_code();

// Copy the last error/response body from weather fetch (for debugging)
// into out, NUL-terminated
void weather_get_last_response(char* out, size_t out_len);

// Inject mock weather data for testing animations (condition_id: 200=thunder, 300=drizzle, 500=rain, 600=snow)
void weather_set_mock(float temp, const char* desc, int condition_id);

#endif // WEATHER_CLIENT_H
