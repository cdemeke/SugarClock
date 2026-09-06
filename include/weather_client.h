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

// Called from the background network task to poll weather on schedule.
// Replaces weather_loop() when the net_task is in use.
void weather_poll_tick();

// Get the latest weather reading (thread-safe copy).
WeatherReading weather_get_reading();

// Check if weather data has been received
bool weather_has_data();

// Force an immediate weather fetch on the network task.
// Returns true on success, false on failure or timeout.
// `timeout_ms` controls how long to wait for the background task.
bool weather_force_fetch(unsigned long timeout_ms);

// Get the last HTTP status code from weather fetch
int weather_get_last_http_code();

// Copy the last error/response body into `out` (thread-safe).
void weather_get_last_response(char* out, size_t out_len);

// Inject mock weather data for testing animations (condition_id: 200=thunder, 300=drizzle, 500=rain, 600=snow)
void weather_set_mock(float temp, const char* desc, int condition_id);

// Register a callback invoked just before a blocking weather fetch
// (used by the engine to clear animations before the HTTP call blocks)
typedef void (*WeatherPreFetchCallback)();
void weather_set_pre_fetch_callback(WeatherPreFetchCallback cb);

// Pause background weather traffic during OTA download.
void weather_set_paused(bool paused);
bool weather_is_paused();

#endif // WEATHER_CLIENT_H
