#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

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

// Non-blocking polling loop
void weather_loop();

// Get the latest weather reading
const WeatherReading& weather_get_reading();

// Check if weather data has been received
bool weather_has_data();

// Force an immediate weather fetch (for testing), returns true on success
bool weather_force_fetch();

// Get the last HTTP status code from weather fetch
int weather_get_last_http_code();

// Get the last error/response body from weather fetch (for debugging)
const char* weather_get_last_response();

// Inject mock weather data for testing animations (condition_id: 200=thunder, 300=drizzle, 500=rain, 600=snow)
void weather_set_mock(float temp, const char* desc, int condition_id);

// Register a callback invoked just before a blocking weather fetch
// (used by the engine to clear animations before the HTTP call blocks)
typedef void (*WeatherPreFetchCallback)();
void weather_set_pre_fetch_callback(WeatherPreFetchCallback cb);

#endif // WEATHER_CLIENT_H
