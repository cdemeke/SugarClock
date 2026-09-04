#include "ambient_ghost.h"

#include "config_manager.h"
#include "display.h"
#include "http_client.h"
#include "time_engine.h"
#include "weather_client.h"

#include <Arduino.h>

namespace {

constexpr unsigned long GHOST_FRAME_MS = 100; // restrained 10 FPS
constexpr unsigned long INTERACTION_MS = 1800;
constexpr int GLUCOSE_FAILURE_STALE_COUNT = 5;

enum GhostPose {
    GHOST_FLOATING,
    GHOST_RESTING,
    GHOST_WAVING
};

enum GhostWeather {
    GHOST_WEATHER_QUIET,
    GHOST_WEATHER_RAIN,
    GHOST_WEATHER_COLD,
    GHOST_WEATHER_SUN,
    GHOST_WEATHER_WIND
};

enum GlucoseEffect {
    GLUCOSE_EFFECT_NONE,
    GLUCOSE_EFFECT_MISSING,
    GLUCOSE_EFFECT_IN_RANGE,
    GLUCOSE_EFFECT_LOW,
    GLUCOSE_EFFECT_HIGH,
    GLUCOSE_EFFECT_URGENT_LOW,
    GLUCOSE_EFFECT_URGENT_HIGH
};

static bool interaction_active = false;
static unsigned long interaction_started_ms = 0;

static uint16_t lavender()       { return display_color(180, 140, 255); }
static uint16_t soft_lavender()  { return display_color(146, 111, 214); }
static uint16_t bright_lavender(){ return display_color(216, 197, 255); }
static uint16_t cool()           { return display_color(151, 218, 242); }
static uint16_t rain_blue()      { return display_color(48, 154, 198); }
static uint16_t gold()           { return display_color(255, 190, 66); }
static uint16_t orange()         { return display_color(244, 118, 42); }
static uint16_t red()            { return display_color(232, 60, 54); }

static uint16_t packed_color(uint32_t color) {
    return display_color((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

static void hline(int x1, int x2, int y, uint16_t color) {
    for (int x = x1; x <= x2; ++x) display_draw_pixel(x, y, color);
}

static bool hour_in_window(int hour, int start_hour, int end_hour) {
    if (start_hour == end_hour) return false;
    if (start_hour > end_hour) return hour >= start_hour || hour < end_hour;
    return hour >= start_hour && hour < end_hour;
}

static void rest_hours(int& start_hour, int& end_hour) {
    const AppConfig& cfg = config_get();
    if (cfg.night_mode_enabled) {
        start_hour = cfg.night_start_hour;
        end_hour = cfg.night_end_hour;
        if (start_hour < 0 || start_hour > 23) start_hour = 22;
        if (end_hour < 0 || end_hour > 23) end_hour = 7;
        return;
    }
    start_hour = 22;
    end_hour = 7;
}

static GhostPose choose_pose() {
    unsigned long now = millis();
    if (interaction_active) {
        if (now - interaction_started_ms < INTERACTION_MS) return GHOST_WAVING;
        interaction_active = false;
    }

    if (!time_is_available()) return GHOST_FLOATING;

    int start_hour;
    int end_hour;
    rest_hours(start_hour, end_hour);
    int hour = time_get_hour();
    if (hour_in_window(hour, start_hour, end_hour) || hour == 13) {
        return GHOST_RESTING;
    }
    return GHOST_FLOATING;
}

static bool weather_is_fresh() {
    const AppConfig& cfg = config_get();
    if (!cfg.weather_enabled || !weather_has_data()) return false;

    const WeatherReading& reading = weather_get_reading();
    if (!reading.valid) return false;

    int poll_minutes = cfg.weather_poll_min;
    if (poll_minutes < 5) poll_minutes = 5;
    if (poll_minutes > 180) poll_minutes = 180;
    unsigned long fresh_for_ms = (unsigned long)poll_minutes * 2UL * 60UL * 1000UL;
    const unsigned long minimum_fresh_ms = 30UL * 60UL * 1000UL;
    if (fresh_for_ms < minimum_fresh_ms) fresh_for_ms = minimum_fresh_ms;
    return millis() - reading.received_at_ms <= fresh_for_ms;
}

static GhostWeather choose_weather() {
    if (!weather_is_fresh()) return GHOST_WEATHER_QUIET;

    const WeatherReading& reading = weather_get_reading();
    const AppConfig& cfg = config_get();
    int id = reading.condition_id;
    if (id >= 200 && id < 600) return GHOST_WEATHER_RAIN;
    if (id >= 600 && id < 700) return GHOST_WEATHER_COLD;

    float cold_threshold = cfg.weather_use_f ? 45.0f : 7.0f;
    float hot_threshold = cfg.weather_use_f ? 80.0f : 27.0f;
    if (reading.temp <= cold_threshold) return GHOST_WEATHER_COLD;
    if (id == 771) return GHOST_WEATHER_WIND;
    if (id == 800 || reading.temp >= hot_threshold) return GHOST_WEATHER_SUN;
    return GHOST_WEATHER_QUIET;
}

static GlucoseEffect choose_glucose_effect() {
    const GlucoseReading& reading = http_get_reading();
    const AppConfig& cfg = config_get();
    if (!reading.valid) return GLUCOSE_EFFECT_MISSING;

    if (cfg.data_source != 2) {
        unsigned long stale_ms = (unsigned long)cfg.stale_timeout_min * 60UL * 1000UL;
        if (!http_has_ever_received() ||
            http_time_since_last_reading() >= stale_ms ||
            http_get_failure_count() >= GLUCOSE_FAILURE_STALE_COUNT) {
            return GLUCOSE_EFFECT_NONE;
        }
    }

    if (reading.glucose < cfg.thresh_urgent_low) return GLUCOSE_EFFECT_URGENT_LOW;
    if (reading.glucose < cfg.thresh_low) return GLUCOSE_EFFECT_LOW;
    if (reading.glucose <= cfg.thresh_high) return GLUCOSE_EFFECT_IN_RANGE;
    if (reading.glucose <= cfg.thresh_urgent_high) return GLUCOSE_EFFECT_HIGH;
    return GLUCOSE_EFFECT_URGENT_HIGH;
}

static void draw_ghost(unsigned long frame, bool resting, bool waving) {
    // This idle silhouette exactly matches the earlier 32x8 ghost concept.
    static const char* const sprite[8] = {
        "............LLLLLLLL............",
        "..........LLLLLLLLLLLL..........",
        ".........LLLLLLLLLLLLLL.........",
        "........LLLL.LLLLLL.LLLL........",
        ".....LLLLLLLLLLLLLLLLLLLLLL.....",
        "........LLLLLLL..LLLLLLL........",
        "........LLLLLLLLLLLLLLLL........",
        "........LLL.LLL.LLL.LLLL........"
    };

    bool shimmer = ((frame / 12) & 1) != 0;
    uint16_t body = resting ? soft_lavender()
        : (shimmer ? bright_lavender() : lavender());

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 32; ++x) {
            if (sprite[y][x] != 'L') continue;
            // The two arms move independently during the wave animation.
            if (y == 4 && ((x >= 5 && x <= 6) || (x >= 25 && x <= 26))) continue;
            display_draw_pixel(x, y, body);
        }
    }

    if (waving) {
        int arm_y = ((frame / 2) & 1) ? 3 : 5;
        display_draw_pixel(5, arm_y, body);
        display_draw_pixel(6, arm_y, body);
        display_draw_pixel(25, 7 - arm_y, body);
        display_draw_pixel(26, 7 - arm_y, body);
    } else {
        display_draw_pixel(5, 4, body);
        display_draw_pixel(6, 4, body);
        display_draw_pixel(25, 4, body);
        display_draw_pixel(26, 4, body);
    }

    if (resting) {
        // Wider dark eye lines and no mouth make the original face read asleep.
        hline(11, 13, 3, 0);
        hline(18, 20, 3, 0);
        display_draw_pixel(15, 5, body);
        display_draw_pixel(16, 5, body);
        display_draw_pixel(25, 2, cool());
        display_draw_pixel(27, 1, cool());
    }
}

static void draw_weather(GhostWeather weather, unsigned long frame) {
    switch (weather) {
        case GHOST_WEATHER_RAIN: {
            static const uint8_t xs[] = {1, 4, 28, 31};
            for (unsigned int i = 0; i < sizeof(xs); ++i) {
                int y = (int)((frame / 2 + i * 2) % 8);
                display_draw_pixel(xs[i], y, rain_blue());
            }
            break;
        }
        case GHOST_WEATHER_COLD:
            // A pale scarf crosses the body and trails off on the right.
            hline(8, 23, 5, cool());
            display_draw_pixel(24, 6, cool());
            display_draw_pixel(25, 7, cool());
            break;
        case GHOST_WEATHER_SUN:
            display_draw_pixel(2, 0, gold());
            hline(1, 3, 1, gold());
            display_draw_pixel(2, 2, gold());
            break;
        case GHOST_WEATHER_WIND: {
            int offset = (int)((frame / 3) % 4);
            hline(offset, offset + 4, 2, rain_blue());
            hline(27 - offset, 31 - offset, 6, rain_blue());
            break;
        }
        case GHOST_WEATHER_QUIET:
        default:
            break;
    }
}

static void draw_glucose_motion(GlucoseEffect effect, unsigned long frame) {
    const AppConfig& cfg = config_get();
    if (effect == GLUCOSE_EFFECT_IN_RANGE) {
        // A sparse sparkle and short wave burst indicate fresh in-range data.
        uint16_t color = packed_color(cfg.color_in_range);
        if ((frame / 8) % 10 < 6) {
            display_draw_pixel(2, 0, color);
            display_draw_pixel(1, 1, color);
            display_draw_pixel(3, 1, color);
            display_draw_pixel(2, 2, color);
        }
        return;
    }

    if (effect != GLUCOSE_EFFECT_LOW && effect != GLUCOSE_EFFECT_HIGH) return;

    uint16_t color = packed_color(effect == GLUCOSE_EFFECT_LOW
        ? cfg.color_low
        : cfg.color_high);
    int phase = (int)((frame / 3) % 8);
    bool downward = effect == GLUCOSE_EFFECT_LOW;
    for (int i = 0; i < 3; ++i) {
        int left_y = downward ? (phase + i) % 8 : (7 - phase - i + 16) % 8;
        int right_y = downward ? (phase + i + 4) % 8 : (11 - phase - i + 16) % 8;
        display_draw_pixel(i, left_y, color);
        display_draw_pixel(29 + i, right_y, color);
    }
}

static void draw_urgent_number(GlucoseEffect effect) {
    const GlucoseReading& reading = http_get_reading();
    const AppConfig& cfg = config_get();
    uint32_t packed = effect == GLUCOSE_EFFECT_URGENT_LOW
        ? cfg.color_urgent_low
        : cfg.color_urgent_high;

    char number[8];
    snprintf(number, sizeof(number), "%d", reading.glucose);
    int width = display_text_width(number) - 1;
    int x = (32 - width) / 2;
    display_draw_text(number, x, 0, packed_color(packed));
}

static void draw_seasonal_surprise(unsigned long frame) {
    const AppConfig& cfg = config_get();
    if (!cfg.ambient_seasonal || !time_is_available()) return;

    int month = time_get_month();
    int day = time_get_day();
    if (month == 10 && day == 31) {
        // A tiny orange pumpkin glow complements the ghost without a costume.
        display_draw_pixel(29, 5, orange());
        hline(28, 30, 6, orange());
        display_draw_pixel(29, 7, orange());
    } else if ((month == 12 && day == 31) || (month == 1 && day == 1)) {
        static const uint8_t xs[] = {1, 4, 28, 31};
        for (unsigned int i = 0; i < sizeof(xs); ++i) {
            int y = (int)((frame / 3 + i * 2) % 7);
            display_draw_pixel(xs[i], y, (i & 1) ? red() : gold());
        }
    }
}

} // namespace

void ambient_ghost_init() {
    interaction_active = false;
    interaction_started_ms = 0;
}

void ambient_ghost_render() {
    unsigned long frame = millis() / GHOST_FRAME_MS;
    GhostPose pose = choose_pose();
    GlucoseEffect glucose_effect = choose_glucose_effect();

    display_clear();

    if (glucose_effect == GLUCOSE_EFFECT_MISSING) {
        display_draw_text("---", 7, 0, packed_color(config_get().color_stale));
        return;
    }

    // Urgent readings own the whole display, exactly as they do for the fish.
    if (glucose_effect == GLUCOSE_EFFECT_URGENT_LOW ||
        glucose_effect == GLUCOSE_EFFECT_URGENT_HIGH) {
        draw_urgent_number(glucose_effect);
        return;
    }

    bool lively_burst = glucose_effect == GLUCOSE_EFFECT_IN_RANGE && frame % 80 < 16;
    draw_ghost(frame,
               pose == GHOST_RESTING,
               pose == GHOST_WAVING || (pose == GHOST_FLOATING && lively_burst));
    draw_weather(choose_weather(), frame);
    draw_glucose_motion(glucose_effect, frame);
    draw_seasonal_surprise(frame);
}

void ambient_ghost_interact() {
    interaction_active = true;
    interaction_started_ms = millis();
}
