#include "ambient_fish.h"

#include "config_manager.h"
#include "display.h"
#include "http_client.h"
#include "time_engine.h"
#include "weather_client.h"

#include <Arduino.h>

namespace {

constexpr unsigned long FISH_FRAME_MS = 100; // restrained 10 FPS
constexpr unsigned long INTERACTION_MS = 1800;
constexpr int GLUCOSE_FAILURE_STALE_COUNT = 5;

enum FishPose {
    FISH_SWIMMING,
    FISH_RESTING,
    FISH_PLAYING
};

enum WaterWeather {
    WATER_QUIET,
    WATER_RAIN,
    WATER_COLD,
    WATER_SUN,
    WATER_CURRENT
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

static uint16_t orange()    { return display_color(244, 118, 42); }
static uint16_t gold()      { return display_color(255, 190, 66); }
static uint16_t deep_water(){ return display_color(16, 64, 84); }
static uint16_t water()     { return display_color(48, 154, 198); }
static uint16_t cool()      { return display_color(151, 218, 242); }
static uint16_t purple()    { return display_color(148, 75, 205); }
static uint16_t red()       { return display_color(232, 60, 54); }

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

static FishPose choose_pose() {
    unsigned long now = millis();
    if (interaction_active) {
        if (now - interaction_started_ms < INTERACTION_MS) return FISH_PLAYING;
        interaction_active = false;
    }

    // Without a trustworthy clock the time-based pose simply stays awake;
    // connectivity and missing-data warnings are handled by the display engine.
    if (!time_is_available()) return FISH_SWIMMING;

    int start_hour;
    int end_hour;
    rest_hours(start_hour, end_hour);
    int hour = time_get_hour();
    if (hour_in_window(hour, start_hour, end_hour)) return FISH_RESTING;

    // A quiet afternoon rest gives the fish a daily rhythm without turning
    // the companion into something the user has to care for.
    return hour == 13
        ? FISH_RESTING
        : FISH_SWIMMING;
}

static bool weather_is_fresh() {
    const AppConfig& cfg = config_get();
    if (!cfg.weather_enabled || !weather_has_data()) return false;

    const WeatherReading& weather_reading = weather_get_reading();
    if (!weather_reading.valid) return false;

    // Allow one missed poll, while ensuring stale conditions quietly disappear.
    int poll_minutes = cfg.weather_poll_min;
    if (poll_minutes < 5) poll_minutes = 5;
    if (poll_minutes > 180) poll_minutes = 180;
    unsigned long fresh_for_ms = (unsigned long)poll_minutes * 2UL * 60UL * 1000UL;
    const unsigned long minimum_fresh_ms = 30UL * 60UL * 1000UL;
    if (fresh_for_ms < minimum_fresh_ms) fresh_for_ms = minimum_fresh_ms;
    return millis() - weather_reading.received_at_ms <= fresh_for_ms;
}

static WaterWeather choose_water_weather() {
    if (!weather_is_fresh()) return WATER_QUIET;

    const WeatherReading& weather_reading = weather_get_reading();
    const AppConfig& cfg = config_get();
    int id = weather_reading.condition_id;
    if (id >= 200 && id < 600) return WATER_RAIN;
    if (id >= 600 && id < 700) return WATER_COLD;

    float cold_threshold = cfg.weather_use_f ? 45.0f : 7.0f;
    float hot_threshold = cfg.weather_use_f ? 80.0f : 27.0f;
    if (weather_reading.temp <= cold_threshold) return WATER_COLD;
    if (id == 771) return WATER_CURRENT; // squall
    if (id == 800 || weather_reading.temp >= hot_threshold) return WATER_SUN;
    return WATER_QUIET;
}

static GlucoseEffect choose_glucose_effect() {
    const GlucoseReading& reading = http_get_reading();
    const AppConfig& cfg = config_get();
    if (!reading.valid) return GLUCOSE_EFFECT_MISSING;

    // Usually the engine replaces Ambient Fish with its NO DATA or STALE
    // screen first. This local guard also keeps forced/debug rendering quiet.
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

static void draw_water_weather(WaterWeather weather_state, unsigned long frame) {
    // Keep these effects sparse: the fish remains the dominant silhouette.
    switch (weather_state) {
        case WATER_RAIN: {
            static const uint8_t drops[] = {2, 12, 27};
            for (unsigned int i = 0; i < sizeof(drops); ++i) {
                int y = (int)((frame / 2 + i * 2) % 2);
                display_draw_pixel(drops[i], y, cool());
            }
            hline(0, 4, 2, deep_water());
            hline(26, 31, 2, deep_water());
            break;
        }
        case WATER_COLD: {
            static const uint8_t xs[] = {2, 5, 28, 30};
            for (unsigned int i = 0; i < sizeof(xs); ++i) {
                int y = 1 + (int)((frame / 8 + i * 2) % 5);
                display_draw_pixel(xs[i], y, cool());
            }
            break;
        }
        case WATER_SUN:
            hline(1, 5, 0, gold());
            display_draw_pixel(2, 1, gold());
            display_draw_pixel(4, 1, gold());
            break;
        case WATER_CURRENT: {
            int offset = (int)((frame / 3) % 4);
            hline(offset, offset + 4, 1, water());
            hline(27 - offset, 31 - offset, 6, deep_water());
            break;
        }
        case WATER_QUIET:
        default:
            break;
    }
}

static void draw_glucose_current(GlucoseEffect effect, unsigned long frame) {
    const AppConfig& cfg = config_get();

    if (effect == GLUCOSE_EFFECT_IN_RANGE) {
        // A sparse green sparkle and extra bubble make in-range feel playful.
        uint16_t color = packed_color(cfg.color_in_range);
        int bubble_y = 4 - (int)((frame / 10) % 3);
        display_draw_pixel(5, bubble_y, color);
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

    // Diagonal streams stay at the edges so the fish remains calm and legible.
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
    int width = display_text_width(number) - 1; // omit the final glyph spacing
    int x = (32 - width) / 2;
    display_draw_text(number, x, 0, packed_color(packed));
}

static void draw_missing_glucose() {
    display_draw_text("---", 7, 0, packed_color(config_get().color_stale));
}

static void draw_fish(unsigned long frame, bool resting, bool playful) {
    // This exact idle frame is shared with the settings preview. Its broad
    // silhouette uses nearly the full width so it remains legible at a glance.
    static const char* const sprite[8] = {
        "....C...................Y.......",
        "..........OOOOOOOOO....Y........",
        "..C.....OOOOOOOOOOOOOOY.........",
        ".......O.OOOOOOOOOOOOOOO........",
        "........OOOOOOOOOOOOOOY.........",
        "..........OOOOOOOOO....Y........",
        "............CCCCC.......Y.......",
        "................................"
    };

    bool tail_flick = playful ? ((frame / 2) & 1) : ((frame / 12) & 1);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 32; ++x) {
            char pixel = sprite[y][x];

            // The two cyan pixels ahead of the mouth are animated separately.
            if (pixel == 'C' && x < 8) continue;
            // Every other tail pose substitutes a slightly wider fan below.
            if (pixel == 'Y' && tail_flick) continue;

            uint16_t color;
            switch (pixel) {
                case 'O': color = orange(); break;
                case 'Y': color = gold(); break;
                case 'C': color = cool(); break;
                default: continue;
            }
            display_draw_pixel(x, y, color);
        }
    }

    if (tail_flick) {
        display_draw_pixel(24, 1, gold());
        display_draw_pixel(23, 2, gold());
        display_draw_pixel(22, 3, gold());
        display_draw_pixel(23, 4, gold());
        display_draw_pixel(24, 5, gold());
        display_draw_pixel(25, 6, gold());
    }

    if (resting) {
        // Filling the open eye and adding a two-pixel dark line reads as closed.
        display_draw_pixel(8, 3, orange());
        display_draw_pixel(8, 4, 0);
        display_draw_pixel(9, 4, 0);
    }
}

static void draw_bubbles(unsigned long frame, bool playful) {
    if (playful) {
        // The button response sends three bubbles forward from the mouth.
        unsigned long response_frame = (millis() - interaction_started_ms) / FISH_FRAME_MS;
        display_draw_pixel(6 - (int)((response_frame / 3) % 4), 3, cool());
        display_draw_pixel(4 - (int)((response_frame / 5) % 3), 1, water());
        display_draw_pixel(1, 3 - (int)((response_frame / 6) % 3), cool());
        return;
    }

    // Two slow bubbles match the preview and move only occasionally.
    int drift = (int)((frame / 12) % 3);
    display_draw_pixel(4, (3 - drift) % 3, cool());
    display_draw_pixel(2, 2 - (drift / 2), water());
}

static void draw_swimming_fish(unsigned long frame, bool interacting, bool in_range) {
    // In-range liveliness comes in short bursts; direct interaction is immediate.
    bool lively_burst = in_range && (frame % 80 < 16);
    draw_fish(frame, false, interacting || lively_burst);
    draw_bubbles(frame, interacting);

}

static void draw_resting_fish(unsigned long frame) {
    draw_fish(frame / 2, true, false);
    hline(5, 25, 7, deep_water());

    // A single slow bubble replaces a busy sleep symbol.
    display_draw_pixel(4, 3 - (int)((frame / 16) % 3), water());
}

static void draw_seasonal_surprise(unsigned long frame) {
    const AppConfig& cfg = config_get();
    if (!cfg.ambient_seasonal || !time_is_available()) return;

    int month = time_get_month();
    int day = time_get_day();
    if (month == 10 && day == 31) {
        // A tiny purple moon and orange sparkle suggest Halloween without
        // covering or costuming the fish.
        display_draw_pixel(29, 0, purple());
        hline(28, 30, 1, purple());
        display_draw_pixel(29, 2, purple());
        display_draw_pixel(2, 6, orange());
    } else if ((month == 12 && day == 31) || (month == 1 && day == 1)) {
        // Sparse deterministic confetti keeps New Year's celebratory but calm.
        static const uint8_t xs[] = {1, 4, 28, 31};
        for (unsigned int i = 0; i < sizeof(xs); ++i) {
            int y = (int)((frame / 3 + i * 2) % 7);
            display_draw_pixel(xs[i], y, (i & 1) ? red() : gold());
        }
    }
}

} // namespace

void ambient_fish_init() {
    interaction_active = false;
    interaction_started_ms = 0;
}

void ambient_fish_render() {
    unsigned long frame = millis() / FISH_FRAME_MS;
    FishPose pose = choose_pose();
    GlucoseEffect glucose_effect = choose_glucose_effect();

    display_clear();

    if (glucose_effect == GLUCOSE_EFFECT_MISSING) {
        draw_missing_glucose();
        return;
    }

    // Urgent readings get the entire display: no fish, current, weather, trend,
    // or seasonal decoration competes with the number.
    if (glucose_effect == GLUCOSE_EFFECT_URGENT_LOW ||
        glucose_effect == GLUCOSE_EFFECT_URGENT_HIGH) {
        draw_urgent_number(glucose_effect);
        return;
    }

    draw_water_weather(choose_water_weather(), frame);
    draw_glucose_current(glucose_effect, frame);

    if (pose == FISH_RESTING) {
        draw_resting_fish(frame);
    } else {
        draw_swimming_fish(frame,
                           pose == FISH_PLAYING,
                           glucose_effect == GLUCOSE_EFFECT_IN_RANGE);
    }

    draw_seasonal_surprise(frame);
}

void ambient_fish_interact() {
    interaction_active = true;
    interaction_started_ms = millis();
}
