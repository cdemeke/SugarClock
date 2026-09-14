#ifndef WEATHER_RENDER_H
#define WEATHER_RENDER_H

#include <stddef.h>
#include <stdint.h>

enum class WeatherVisual {
    Clear, PartlyCloudy, Cloudy, Drizzle, Rain, Sleet, Snow, Storm,
    Tornado, CloudFallback
};

WeatherVisual weather_visual_for_condition(int condition_id);

// Round half away from zero. Writes numeric digits only. Returns false for
// nonfinite values, rounded values outside [-999, 9999], or a short buffer.
bool weather_format_temperature(float temp, char* out, size_t size);

// Pure drawing call: caller clears/shows the frame and owns the animation clock.
// Icons occupy x=0..7; x=8..9 stays empty; temperature occupies x=10..31.
// Uses 4x7 digits, or 3x5 when needed to fit. Invalid temperatures show a static
// unavailable dash and --F/--C. Temperature is already in the requested unit.
void weather_render(float temp, bool use_f, int condition_id,
                    uint32_t elapsed_ms, uint16_t text_color);

#endif
