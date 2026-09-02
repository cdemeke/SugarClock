#include "ambient_cat.h"

#include "config_manager.h"
#include "display.h"
#include "time_engine.h"
#include "weather_client.h"

#include <Arduino.h>

namespace {

constexpr unsigned long CAT_FRAME_MS = 100;       // deliberately restrained 10 FPS
constexpr unsigned long INTERACTION_MS = 1800;

enum CatPose {
    CAT_AWAKE,
    CAT_SLEEPING,
    CAT_NAPPING,
    CAT_STRETCHING,
    CAT_YAWNING,
    CAT_INTERACTING
};

enum WeatherDress {
    DRESS_NONE,
    DRESS_RAIN,
    DRESS_SNOW,
    DRESS_SUN,
    DRESS_WIND
};

static bool interaction_active = false;
static unsigned long interaction_started_ms = 0;

static uint16_t orange()    { return display_color(232, 120, 50); }
static uint16_t gold()      { return display_color(255, 185, 70); }
static uint16_t cream()     { return display_color(255, 208, 138); }
static uint16_t outline()   { return display_color(109, 48, 29); }
static uint16_t brown()     { return display_color(26, 15, 11); }
static uint16_t shadow()    { return display_color(42, 18, 9); }
static uint16_t rain_blue() { return display_color(45, 135, 235); }
static uint16_t snow_blue() { return display_color(190, 225, 255); }
static uint16_t scarf_red() { return display_color(225, 48, 42); }
static uint16_t purple()    { return display_color(155, 74, 220); }

static void hline(int x1, int x2, int y, uint16_t color) {
    for (int x = x1; x <= x2; ++x) display_draw_pixel(x, y, color);
}

static bool hour_in_window(int hour, int start_hour, int end_hour) {
    if (start_hour == end_hour) return false;
    if (start_hour > end_hour) return hour >= start_hour || hour < end_hour;
    return hour >= start_hour && hour < end_hour;
}

static void sleep_hours(int& start_hour, int& end_hour) {
    const AppConfig& cfg = config_get();
    if (cfg.night_mode_enabled) {
        start_hour = cfg.night_start_hour;
        end_hour = cfg.night_end_hour;
        if (start_hour < 0 || start_hour > 23) start_hour = 22;
        if (end_hour < 0 || end_hour > 23) end_hour = 7;
    } else {
        start_hour = 22;
        end_hour = 7;
    }
}

static int minutes_since(int minute_of_day, int reference_minute) {
    int result = minute_of_day - reference_minute;
    if (result < 0) result += 24 * 60;
    return result;
}

static CatPose choose_pose(unsigned long frame) {
    unsigned long now = millis();
    if (interaction_active) {
        if (now - interaction_started_ms < INTERACTION_MS) return CAT_INTERACTING;
        interaction_active = false;
    }

    // With no trustworthy clock, remain a calm, ordinary cat. There is no
    // error pose and no attempt to infer behavior from glucose or connectivity.
    if (!time_is_available()) return CAT_AWAKE;

    int start_hour;
    int end_hour;
    sleep_hours(start_hour, end_hour);
    int hour = time_get_hour();
    int minute = time_get_minute();
    if (hour_in_window(hour, start_hour, end_hour)) return CAT_SLEEPING;

    // A short, predictable afternoon nap gives the cat a daily rhythm without
    // creating a pet-care mechanic for the user.
    if (hour == 13) return CAT_NAPPING;

    const int minute_of_day = hour * 60 + minute;
    const unsigned long ambient_cycle = frame % 120; // 12-second gentle cycle

    // Stretch occasionally during the first 20 minutes after waking.
    if (minutes_since(minute_of_day, end_hour * 60) < 20 && ambient_cycle < 24) {
        return CAT_STRETCHING;
    }

    // Yawn occasionally during the hour leading into bedtime.
    int until_bed = start_hour * 60 - minute_of_day;
    if (until_bed < 0) until_bed += 24 * 60;
    if (until_bed <= 60 && ambient_cycle < 18) return CAT_YAWNING;

    return CAT_AWAKE;
}

static bool weather_is_fresh() {
    const AppConfig& cfg = config_get();
    if (!cfg.weather_enabled || !weather_has_data()) return false;

    const WeatherReading& weather = weather_get_reading();
    if (!weather.valid) return false;

    // Two polling intervals (at least 30 minutes) keeps an old outfit from
    // lingering after weather updates stop, while tolerating one missed poll.
    int poll_minutes = cfg.weather_poll_min;
    if (poll_minutes < 5) poll_minutes = 5;
    if (poll_minutes > 180) poll_minutes = 180;
    unsigned long fresh_for_ms = (unsigned long)poll_minutes * 2UL * 60UL * 1000UL;
    const unsigned long minimum_fresh_ms = 30UL * 60UL * 1000UL;
    if (fresh_for_ms < minimum_fresh_ms) fresh_for_ms = minimum_fresh_ms;
    return millis() - weather.received_at_ms <= fresh_for_ms;
}

static WeatherDress choose_weather_dress() {
    if (!weather_is_fresh()) return DRESS_NONE;

    const WeatherReading& weather = weather_get_reading();
    const AppConfig& cfg = config_get();
    int id = weather.condition_id;
    if ((id >= 200 && id < 600)) return DRESS_RAIN;
    if (id >= 600 && id < 700) return DRESS_SNOW;

    float cold_threshold = cfg.weather_use_f ? 45.0f : 7.0f;
    float hot_threshold = cfg.weather_use_f ? 80.0f : 27.0f;
    if (weather.temp <= cold_threshold) return DRESS_SNOW;
    // Most 7xx codes are haze, fog, smoke, dust, or ash. Only a squall is a
    // reliable signal for the wind animation; unsupported conditions stay quiet.
    if (id == 771) return DRESS_WIND;
    if (id == 800 || weather.temp >= hot_threshold) return DRESS_SUN;
    return DRESS_NONE;
}

static void draw_awake_cat(unsigned long frame, bool looking_up, bool yawning) {
    const unsigned long cycle = frame % 160; // 16 seconds
    bool blinking = !looking_up && !yawning && cycle >= 52 && cycle < 54;
    bool tail_high = looking_up || (cycle >= 102 && cycle < 110 && ((frame / 2) & 1));
    bool breathe = ((frame / 8) & 1) != 0;

    // This exact outlined idle sprite is also used by the settings preview.
    // Its side profile, ears, cream underside, and curled tail stay readable
    // even when the matrix is viewed from across a room.
    static const char* const sprite[8] = {
        "........D.D..D.D................",
        "........DAD..DAD................",
        ".......DAADDDDAADDDDDD..........",
        "......DACAAAAAAAAAAAADD...DD....",
        "......DAEAAAAAAAAAAAAD...DAD....",
        "......DAAAAAAAAAAAAAADDDDAD.....",
        ".......DCCCCCCCAAAAAAAAAD.......",
        "........DDDDDDDDDDDDDDD........."
    };
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 32; ++x) {
            uint16_t color;
            switch (sprite[y][x]) {
                case 'D': color = outline(); break;
                case 'A': color = orange(); break;
                case 'C': color = cream(); break;
                case 'E': color = brown(); break;
                default: continue;
            }
            display_draw_pixel(x, y, color);
        }
    }

    if (breathe) display_draw_pixel(20, 3, gold());

    // Curling tail on the right, with a tiny deterministic flick. Redrawing
    // these few pixels is enough to animate without making the whole cat busy.
    if (tail_high) {
        display_draw_pixel(25, 4, 0);
        display_draw_pixel(25, 5, 0);
        display_draw_pixel(26, 3, 0);
        display_draw_pixel(26, 4, 0);
        display_draw_pixel(26, 5, 0);
        display_draw_pixel(27, 3, 0);
        display_draw_pixel(27, 4, 0);
        display_draw_pixel(26, 4, outline());
        display_draw_pixel(27, 3, orange());
        display_draw_pixel(28, 2, outline());
        display_draw_pixel(29, 1, outline());
    }

    if (looking_up) {
        display_draw_pixel(8, 4, orange());
        display_draw_pixel(8, 3, brown());
    } else if (blinking || yawning) {
        display_draw_pixel(8, 4, orange());
    }

    if (yawning) {
        display_draw_pixel(7, 5, brown());
        display_draw_pixel(6, 6, display_color(255, 105, 105));
    }
}

static void draw_sleeping_cat(unsigned long frame, bool nap) {
    bool breathe = ((frame / 10) & 1) != 0;
    hline(5, 29, 7, shadow());

    hline(20, 21, 1, orange());
    hline(25, 26, 1, orange());
    hline(19, 27, 2, orange());
    hline(9, 27, 3, orange());
    hline(6, 28, 4, orange());
    hline(5, 28, 5, orange());
    hline(7, 27, 6, orange());
    if (breathe) hline(8, 17, 2, gold());

    display_draw_pixel(21, 1, gold());
    display_draw_pixel(25, 1, gold());
    hline(22, 24, 5, cream());
    hline(22, 23, 3, brown()); // closed eye

    // The tail curls across the body, reinforcing the sleeping silhouette.
    hline(8, 17, 5, gold());
    display_draw_pixel(7, 4, gold());
    display_draw_pixel(17, 4, gold());

    // A tiny drifting Z is the only explicit sleep symbol.
    int z_y = 1 - (int)((frame / 8) % 2);
    uint16_t z_color = nap ? gold() : display_color(110, 150, 220);
    display_draw_pixel(29, z_y, z_color);
    display_draw_pixel(30, z_y, z_color);
    display_draw_pixel(29, z_y + 1, z_color);
    display_draw_pixel(30, z_y + 2, z_color);
}

static void draw_stretching_cat(unsigned long frame) {
    bool reach = ((frame / 4) & 1) != 0;
    hline(3, 31, 7, shadow());
    hline(6, 20, 3, orange());
    hline(5, 23, 4, orange());
    hline(6, 25, 5, orange());
    hline(9, 24, 6, orange());
    hline(22, 23, 1, orange());
    hline(27, 28, 1, orange());
    hline(21, 29, 2, orange());
    hline(21, 30, 3, orange());
    hline(22, 30, 4, orange());
    display_draw_pixel(24, 2, brown());
    display_draw_pixel(28, 2, brown());
    hline(26, 28, 4, cream());

    // Long front paws sell the stretch despite the eight-pixel height.
    hline(reach ? 24 : 23, 31, 6, cream());
    display_draw_pixel(4, 2, orange());
    display_draw_pixel(3, 1, orange());
    display_draw_pixel(2, 0, orange());
}

static void draw_weather_backdrop(WeatherDress dress, unsigned long frame) {
    if (dress == DRESS_RAIN) {
        static const uint8_t xs[] = {1, 4, 8, 26, 30};
        for (unsigned int i = 0; i < sizeof(xs); ++i) {
            int y = (int)((frame + i * 3) % 8);
            display_draw_pixel(xs[i], y, rain_blue());
        }
    } else if (dress == DRESS_SNOW) {
        static const uint8_t xs[] = {1, 4, 8, 27, 31};
        for (unsigned int i = 0; i < sizeof(xs); ++i) {
            int y = (int)((frame / 3 + i * 2) % 7);
            display_draw_pixel(xs[i], y, snow_blue());
        }
    } else if (dress == DRESS_SUN) {
        display_draw_pixel(2, 0, gold());
        hline(1, 3, 1, gold());
        display_draw_pixel(2, 2, gold());
    } else if (dress == DRESS_WIND) {
        hline(0, 4, 1, snow_blue());
        hline(2, 7, 3, snow_blue());
        hline(0, 4, 5, snow_blue());
    }
}

static void draw_weather_accessory(WeatherDress dress, CatPose pose) {
    if (dress == DRESS_RAIN && pose != CAT_SLEEPING && pose != CAT_NAPPING) {
        hline(15, 23, 4, rain_blue());
        hline(15, 23, 5, rain_blue());
        hline(16, 23, 6, rain_blue());
        hline(16, 21, 4, display_color(80, 175, 250));
    } else if (dress == DRESS_SNOW) {
        hline(13, 16, 3, scarf_red());
        hline(14, 16, 4, scarf_red());
        display_draw_pixel(16, 5, scarf_red());
    } else if (dress == DRESS_SUN && pose != CAT_SLEEPING && pose != CAT_NAPPING) {
        hline(7, 10, 4, brown());
        display_draw_pixel(11, 4, outline());
    } else if (dress == DRESS_WIND) {
        hline(13, 16, 4, display_color(70, 210, 190));
        hline(17, 23, 5, display_color(70, 210, 190));
    }
}

static void draw_seasonal_surprise(unsigned long frame) {
    const AppConfig& cfg = config_get();
    if (!cfg.ambient_seasonal || !time_is_available()) return;

    int month = time_get_month();
    int day = time_get_day();
    if (month == 10 && day == 31) {
        // Pumpkin and a purple collar: readable, secular Halloween shorthand.
        hline(1, 4, 4, orange());
        hline(0, 5, 5, orange());
        hline(1, 4, 6, orange());
        display_draw_pixel(2, 3, gold());
        display_draw_pixel(1, 5, brown());
        display_draw_pixel(4, 5, brown());
        display_draw_pixel(2, 6, brown());
        display_draw_pixel(3, 6, brown());
        display_draw_pixel(14, 3, purple());
        display_draw_pixel(15, 4, purple());
        display_draw_pixel(16, 3, purple());
    } else if ((month == 12 && day == 31) || (month == 1 && day == 1)) {
        // Party hat and sparse deterministic confetti for New Year's Eve/day.
        display_draw_pixel(16, 0, gold());
        hline(15, 17, 1, gold());
        hline(14, 18, 2, purple());
        static const uint8_t confetti_x[] = {1, 4, 7, 26, 29, 31};
        for (unsigned int i = 0; i < sizeof(confetti_x); ++i) {
            int y = (int)((frame / 2 + i * 3) % 7);
            uint16_t color = (i & 1) ? scarf_red() : gold();
            display_draw_pixel(confetti_x[i], y, color);
        }
    }
}

} // namespace

void ambient_cat_init() {
    interaction_active = false;
    interaction_started_ms = 0;
}

void ambient_cat_render() {
    unsigned long frame = millis() / CAT_FRAME_MS;
    CatPose pose = choose_pose(frame);
    WeatherDress dress = choose_weather_dress();

    display_clear();
    draw_weather_backdrop(dress, frame);

    switch (pose) {
        case CAT_SLEEPING:
            draw_sleeping_cat(frame, false);
            break;
        case CAT_NAPPING:
            draw_sleeping_cat(frame, true);
            break;
        case CAT_STRETCHING:
            draw_stretching_cat(frame);
            break;
        case CAT_YAWNING:
            draw_awake_cat(frame, false, true);
            break;
        case CAT_INTERACTING:
            draw_awake_cat(frame, true, false);
            break;
        case CAT_AWAKE:
        default:
            draw_awake_cat(frame, false, false);
            break;
    }

    draw_weather_accessory(dress, pose);
    draw_seasonal_surprise(frame);
}

void ambient_cat_interact() {
    interaction_active = true;
    interaction_started_ms = millis();
}
