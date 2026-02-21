#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

struct WeatherReading {
    float temp;
    char description[32];
    int humidity;
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

#endif // WEATHER_CLIENT_H
