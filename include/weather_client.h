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

// Coherent data/diagnostic snapshot, copied under the publication lock.
struct WeatherSnapshot {
    WeatherReading reading{};
    bool ever_received=false;
    int http_code=0;
    unsigned long force_generation=0;
    bool force_ok=false;
    char response[256]={};
};
WeatherSnapshot weather_snapshot();

// Initialize weather client
void weather_init();

// Non-blocking polling loop
void weather_loop();

// Get the latest weather reading
WeatherReading weather_get_reading();

// Check if weather data has been received
bool weather_has_data();

// Queue a weather test through the scheduler; HTTPS runs on a worker.
// Returns false while paused or another test is pending.
// Completion is signalled by weather_force_generation() advancing.
bool weather_force_fetch();
bool weather_force_pending();
bool weather_last_force_ok();
unsigned long weather_force_generation();

// Get the last HTTP status code from weather fetch
int weather_get_last_http_code();

// Error/response text is included in weather_snapshot().

// Inject mock weather data for testing animations (condition_id: 200=thunder, 300=drizzle, 500=rain, 600=snow)
void weather_set_mock(float temp, const char* desc, int condition_id);

// Register an engine-task callback invoked before a weather worker starts.
typedef void (*WeatherPreFetchCallback)();
void weather_set_pre_fetch_callback(WeatherPreFetchCallback cb);

// Pause background weather traffic during OTA download.
void weather_set_paused(bool paused);
bool weather_is_paused();

#endif // WEATHER_CLIENT_H
