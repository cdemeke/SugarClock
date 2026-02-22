#include "glucose_engine.h"
#include "hardware_pins.h"
#include "display.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "time_engine.h"
#include "trend_arrows.h"
#include "weather_client.h"
#include "buzzer.h"
#include "timer_engine.h"
#include "notify_engine.h"
#include "sysmon_engine.h"
#include "countdown_engine.h"
#include <Arduino.h>

#define STALE_WARNING_MS   (10UL * 60 * 1000)   // 10 minutes
#define FAILURE_STALE_COUNT    5
#define FAILURE_NODATA_COUNT   10

static DisplayState current_state = STATE_BOOT;
static DisplayState forced_state = STATE_BOOT;
static bool state_forced = false;
static DisplayState default_mode = STATE_GLUCOSE_DISPLAY;
static DisplayState user_mode = STATE_GLUCOSE_DISPLAY;
static char message_buf[128] = "";
static unsigned long last_render_ms = 0;
static unsigned long boot_start_ms = 0;

// Delta display flash
static int last_seen_glucose = 0;
static unsigned long delta_flash_start_ms = 0;
static bool delta_flash_active = false;
#define DELTA_FLASH_DURATION_MS 3000

// Buzzer alert state (now using shared buzzer module)
static unsigned long alert_snooze_until_ms = 0;
static unsigned long last_beep_ms = 0;
#define BEEP_INTERVAL_MS 10000  // beep every 10 seconds when alerting

#define RENDER_INTERVAL_MS 100  // ~10 FPS

// Helper: convert uint32_t packed RGB to 16-bit display color
static uint16_t color_from_uint32(uint32_t c) {
    return display_color((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

// --- Weather particle animation system ---
struct WeatherParticle {
    int16_t x10;   // fixed-point x * 10
    int16_t y10;   // fixed-point y * 10
    int16_t vx10;  // velocity x * 10
    int16_t vy10;  // velocity y * 10
    bool active;
};

#define MAX_PARTICLES 10
static WeatherParticle particles[MAX_PARTICLES];

// Returns animation type: 0=none, 1=rain, 2=drizzle, 3=snow, 4=thunderstorm
static int weather_anim_type(int condition_id) {
    if (condition_id >= 200 && condition_id < 300) return 4; // thunderstorm
    if (condition_id >= 300 && condition_id < 400) return 2; // drizzle
    if (condition_id >= 500 && condition_id < 600) return 1; // rain
    if (condition_id >= 600 && condition_id < 700) return 3; // snow
    return 0;
}

static void weather_particles_spawn(int anim_type) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].active) continue;

        // Probability of spawning each frame
        if (anim_type == 2 && (random(100) > 15)) continue; // drizzle: fewer
        if (anim_type == 1 && (random(100) > 40)) continue; // rain: moderate
        if (anim_type == 3 && (random(100) > 30)) continue; // snow: moderate
        if (anim_type == 4 && (random(100) > 50)) continue; // thunder: lots

        particles[i].active = true;
        particles[i].x10 = random(0, MATRIX_WIDTH) * 10;
        particles[i].y10 = 0;
        particles[i].vx10 = 0;

        if (anim_type == 3) {
            // Snow: slow, lateral wobble
            particles[i].vy10 = random(5, 12);
            particles[i].vx10 = random(-3, 4);
        } else if (anim_type == 2) {
            // Drizzle: moderate speed
            particles[i].vy10 = random(10, 18);
        } else {
            // Rain / thunderstorm: fast
            particles[i].vy10 = random(15, 26);
        }
    }
}

static void weather_particles_update_and_draw(int anim_type) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;

        particles[i].x10 += particles[i].vx10;
        particles[i].y10 += particles[i].vy10;

        int px = particles[i].x10 / 10;
        int py = particles[i].y10 / 10;

        if (py >= MATRIX_HEIGHT || px < 0 || px >= MATRIX_WIDTH) {
            particles[i].active = false;
            continue;
        }

        // Color based on type
        uint16_t color;
        if (anim_type == 3) {
            color = display_color(200, 200, 255); // snow: white-blue
        } else {
            color = display_color(80, 130, 255);  // rain/drizzle: blue
        }
        display_draw_pixel(px, py, color);
    }
}

// Thunder flash state
static unsigned long next_flash_ms = 0;
static unsigned long flash_end_ms = 0;

// Data-driven toggle order
static DisplayState toggle_order[12];
static int toggle_count = 0;
static int toggle_index = 0;

// Auto-cycle timer
static unsigned long last_cycle_ms = 0;

void engine_rebuild_toggle_order() {
    AppConfig& cfg = config_get();
    toggle_count = 0;
    toggle_order[toggle_count++] = STATE_GLUCOSE_DISPLAY;
    toggle_order[toggle_count++] = STATE_TREND_DISPLAY;
    toggle_order[toggle_count++] = STATE_TIME_DISPLAY;
    if (cfg.weather_enabled) toggle_order[toggle_count++] = STATE_WEATHER_DISPLAY;
    if (cfg.timer_enabled) toggle_order[toggle_count++] = STATE_TIMER_DISPLAY;
    if (cfg.stopwatch_enabled) toggle_order[toggle_count++] = STATE_STOPWATCH_DISPLAY;
    if (cfg.sysmon_enabled && sysmon_has_data()) toggle_order[toggle_count++] = STATE_SYSMON_DISPLAY;
    if (cfg.countdown_enabled) toggle_order[toggle_count++] = STATE_COUNTDOWN_DISPLAY;

    // Reset toggle_index to match current user_mode
    for (int i = 0; i < toggle_count; i++) {
        if (toggle_order[i] == user_mode) {
            toggle_index = i;
            return;
        }
    }
    toggle_index = 0;
}

// Get color for glucose value using custom theme colors from config
static uint16_t themed_glucose_color(int mg_dl, const AppConfig& cfg) {
    uint32_t c;
    if (mg_dl < cfg.thresh_urgent_low) {
        c = cfg.color_urgent_low;
    } else if (mg_dl < cfg.thresh_low) {
        c = cfg.color_low;
    } else if (mg_dl <= cfg.thresh_high) {
        c = cfg.color_in_range;
    } else if (mg_dl <= cfg.thresh_urgent_high) {
        c = cfg.color_high;
    } else {
        c = cfg.color_urgent_high;
    }
    return display_color((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

uint16_t glucose_color(int mg_dl, const GlucoseThresholds& t) {
    if (mg_dl < t.urgent_low) {
        return display_color(255, 0, 0);      // RED - urgent low
    } else if (mg_dl < t.low) {
        return display_color(255, 165, 0);    // ORANGE - low
    } else if (mg_dl <= t.high) {
        return display_color(0, 255, 0);      // GREEN - in range
    } else if (mg_dl <= t.urgent_high) {
        return display_color(255, 165, 0);    // ORANGE - high
    } else {
        return display_color(255, 0, 0);      // RED - urgent high
    }
}

// Check if currently in night mode hours
static bool is_night_mode() {
    AppConfig& cfg = config_get();
    if (!cfg.night_mode_enabled) return false;
    if (!time_is_available()) return false;

    int hour = time_get_hour();
    if (cfg.night_start_hour > cfg.night_end_hour) {
        // Wraps midnight: e.g., 22 to 7
        return (hour >= cfg.night_start_hour || hour < cfg.night_end_hour);
    } else {
        // Same day: e.g., 1 to 6
        return (hour >= cfg.night_start_hour && hour < cfg.night_end_hour);
    }
}

// Get effective brightness considering night mode
static uint8_t effective_brightness() {
    AppConfig& cfg = config_get();
    if (is_night_mode()) {
        return cfg.night_brightness;
    }
    return cfg.brightness;
}

// Handle buzzer alerts
static void check_alerts() {
    AppConfig& cfg = config_get();
    if (!cfg.alert_enabled) return;

    const GlucoseReading& reading = http_get_reading();
    if (!reading.valid) return;

    // Check if snoozed
    if (millis() < alert_snooze_until_ms) return;

    bool should_alert = (reading.glucose < cfg.alert_low || reading.glucose > cfg.alert_high);
    if (!should_alert) return;

    // Beep periodically using shared buzzer module
    if (millis() - last_beep_ms >= BEEP_INTERVAL_MS) {
        last_beep_ms = millis();
        buzzer_beep(1, 2000, 200);
    }
}

// Snooze alerts (called from button handler)
void engine_snooze_alerts() {
    AppConfig& cfg = config_get();
    alert_snooze_until_ms = millis() + (cfg.alert_snooze_min * 60000UL);
    Serial.printf("[ENGINE] Alerts snoozed for %d minutes\n", cfg.alert_snooze_min);
}

void engine_init() {
    current_state = STATE_BOOT;
    boot_start_ms = millis();

    AppConfig& cfg = config_get();
    if (cfg.default_mode == 2) {
        default_mode = STATE_WEATHER_DISPLAY;
    } else if (cfg.default_mode == 1) {
        default_mode = STATE_TIME_DISPLAY;
    } else {
        default_mode = STATE_GLUCOSE_DISPLAY;
    }
    user_mode = default_mode;

    engine_rebuild_toggle_order();

    // Show boot screen
    display_clear();
    display_draw_text("SUGAR", 1, 0, display_color(0, 200, 200));
    display_show();
}

static DisplayState evaluate_state() {
    AppConfig& cfg = config_get();
    unsigned long stale_ms = (unsigned long)cfg.stale_timeout_min * 60UL * 1000UL;

    // Boot screen for first 2 seconds
    if (millis() - boot_start_ms < 2000) {
        return STATE_BOOT;
    }

    // No WiFi
    if (!wifi_is_connected() && config_has_wifi()) {
        return STATE_NO_WIFI;
    }

    // No config
    if (!config_has_server() && !config_has_wifi()) {
        return STATE_NO_CFG;
    }

    // Active notification takes priority
    if (cfg.notify_enabled && notify_has_active()) {
        return STATE_NOTIFY_DISPLAY;
    }

    // Check for NO DATA conditions
    int failures = http_get_failure_count();
    if (failures >= FAILURE_NODATA_COUNT || !http_has_ever_received()) {
        if (config_has_server() && millis() - boot_start_ms > 5000) {
            return STATE_NO_DATA;
        }
    }

    // Check staleness using configurable timeout
    unsigned long age = http_time_since_last_reading();
    if (age >= stale_ms || failures >= FAILURE_STALE_COUNT) {
        return STATE_STALE_WARNING;
    }

    // Server force override
    if (state_forced) {
        return forced_state;
    }

    // Check for server-pushed force_mode
    const GlucoseReading& reading = http_get_reading();
    if (reading.valid && reading.force_mode >= 0) {
        return (DisplayState)reading.force_mode;
    }

    // Check for server message
    if (reading.valid && strlen(reading.message) > 0) {
        strncpy(message_buf, reading.message, sizeof(message_buf) - 1);
        return STATE_MESSAGE_DISPLAY;
    }

    return user_mode;
}

static void render_state(DisplayState state) {
    AppConfig& cfg = config_get();

    switch (state) {
        case STATE_BOOT: {
            display_clear();
            display_draw_text("SUGAR", 1, 0, display_color(0, 200, 200));
            display_show();
            break;
        }

        case STATE_GLUCOSE_DISPLAY: {
            const GlucoseReading& reading = http_get_reading();
            if (!reading.valid) {
                display_clear();
                display_draw_text("---", 7, 0, display_color(100, 100, 100));
                display_show();
                break;
            }

            uint16_t color = themed_glucose_color(reading.glucose, cfg);

            // Check for stale warning (dim + yellow dot)
            unsigned long age = http_time_since_last_reading();
            unsigned long stale_warn_ms = STALE_WARNING_MS;
            unsigned long stale_ms = (unsigned long)cfg.stale_timeout_min * 60UL * 1000UL;
            bool stale_warning = (age >= stale_warn_ms && age < stale_ms);

            if (stale_warning) {
                display_set_brightness(effective_brightness() / 3);
            } else {
                display_set_brightness(effective_brightness());
            }

            // Check if we should show delta flash
            if (cfg.show_delta && reading.glucose != last_seen_glucose && last_seen_glucose > 0) {
                delta_flash_start_ms = millis();
                delta_flash_active = true;
                last_seen_glucose = reading.glucose;
            } else if (last_seen_glucose == 0) {
                last_seen_glucose = reading.glucose;
            }

            // Delta flash: show delta for a few seconds
            if (delta_flash_active && (millis() - delta_flash_start_ms < DELTA_FLASH_DURATION_MS)) {
                display_clear();
                int delta = http_get_delta();
                char dbuf[8];
                if (delta >= 0) {
                    snprintf(dbuf, sizeof(dbuf), "+%d", delta);
                } else {
                    snprintf(dbuf, sizeof(dbuf), "%d", delta);
                }
                int dlen = strlen(dbuf);
                int dx = (MATRIX_WIDTH - dlen * 6) / 2;
                display_draw_text(dbuf, dx, 0, color);
                display_show();
                break;
            }
            delta_flash_active = false;

            // Normal glucose display
            display_draw_glucose(reading.glucose, color);

            // Draw trend arrow to the right of the number
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", reading.glucose);
            int text_len = strlen(buf);
            int total_width = text_len * 6 + 6;
            int x_start = (MATRIX_WIDTH - total_width) / 2;
            int arrow_x = x_start + text_len * 6 + 1;

            if (reading.trend != TREND_UNKNOWN) {
                display_draw_trend(reading.trend, arrow_x, 0, color);
            }

            // Stale warning indicator
            if (stale_warning) {
                display_draw_text("!", MATRIX_WIDTH - 4, 0, display_color(255, 255, 0));
            }

            display_show();
            break;
        }

        case STATE_TIME_DISPLAY: {
            display_set_brightness(effective_brightness());

            if (!time_is_available()) {
                display_clear();
                display_draw_text("--:--", 4, 0, display_color(100, 100, 100));
                display_show();
                break;
            }

            int h = time_get_hour();
            int m = time_get_minute();
            int s = time_get_second();

            // Alternate between time and date every 5 seconds
            bool show_date = cfg.date_on_time_screen && ((millis() / 5000) % 2 == 1);

            if (show_date) {
                display_clear();
                char dbuf[8];
                int day = time_get_day();
                int month = time_get_month();
                if (cfg.date_format == 1) {
                    // MMMDD format
                    const char* abbr = time_get_month_abbr();
                    snprintf(dbuf, sizeof(dbuf), "%s%d", abbr, day);
                } else if (cfg.date_format == 2) {
                    // DD/MM format
                    snprintf(dbuf, sizeof(dbuf), "%d/%d", day, month);
                } else {
                    // M/DD format (default)
                    snprintf(dbuf, sizeof(dbuf), "%d/%d", month, day);
                }
                int len = strlen(dbuf);
                int tx = (MATRIX_WIDTH - len * 6) / 2;
                display_draw_text(dbuf, tx, 0, color_from_uint32(cfg.color_clock));
                display_show();
            } else {
                bool show_colon = (s % 2 == 0);
                display_draw_time(h, m, show_colon, cfg.use_24h, color_from_uint32(cfg.color_clock));
                display_show();
            }
            break;
        }

        case STATE_WEATHER_DISPLAY: {
            display_set_brightness(effective_brightness());
            display_clear();

            if (!weather_has_data()) {
                display_draw_text("WX...", 4, 0, color_from_uint32(cfg.color_weather));
            } else {
                const WeatherReading& wx = weather_get_reading();
                int anim = weather_anim_type(wx.condition_id);

                // Spawn and draw weather particles behind text
                if (anim > 0) {
                    weather_particles_spawn(anim);
                    weather_particles_update_and_draw(anim);

                    // Thunder flash
                    if (anim == 4) {
                        unsigned long now = millis();
                        if (now >= next_flash_ms && flash_end_ms == 0) {
                            flash_end_ms = now + 80;
                            next_flash_ms = now + random(3000, 5001);
                        }
                        if (flash_end_ms > 0 && now < flash_end_ms) {
                            display_flash(255, 255, 255);
                            display_show();
                            break;
                        }
                        if (now >= flash_end_ms) flash_end_ms = 0;
                    }
                }

                char tbuf[8];
                int temp_int = (int)(wx.temp + 0.5f);
                snprintf(tbuf, sizeof(tbuf), "%d*%s", temp_int,
                         config_get().weather_use_f ? "F" : "C");
                int tlen = strlen(tbuf);
                int tx = (MATRIX_WIDTH - tlen * 6) / 2;
                display_draw_text(tbuf, tx, 0, color_from_uint32(cfg.color_weather));
            }

            display_show();
            break;
        }

        case STATE_TIMER_DISPLAY: {
            display_set_brightness(effective_brightness());
            display_clear();

            TimerState ts = timer_get_state();
            int remaining = timer_get_remaining_sec();
            int mm = remaining / 60;
            int ss = remaining % 60;

            if (ts == TIMER_DONE) {
                display_draw_text("DONE!", 1, 0, display_color(0, 255, 0));
            } else if (ts == TIMER_BREAK || ts == TIMER_LONG_BREAK) {
                char tbuf[8];
                snprintf(tbuf, sizeof(tbuf), "B%d:%02d", mm, ss);
                int len = strlen(tbuf);
                int tx = (MATRIX_WIDTH - len * 6) / 2;
                display_draw_text(tbuf, tx, 0, display_color(0, 200, 200));
            } else {
                // IDLE, RUNNING, or PAUSED
                char tbuf[8];
                snprintf(tbuf, sizeof(tbuf), "%d:%02d", mm, ss);
                int len = strlen(tbuf);
                int tx = (MATRIX_WIDTH - len * 6) / 2;

                // Blink when paused
                if (ts == TIMER_PAUSED && (millis() / 500) % 2 == 0) {
                    // Don't draw (blink off)
                } else {
                    display_draw_text(tbuf, tx, 0, display_color(255, 165, 0));
                }
            }

            display_show();
            break;
        }

        case STATE_STOPWATCH_DISPLAY: {
            display_set_brightness(effective_brightness());
            display_clear();

            int elapsed = stopwatch_get_elapsed_sec();
            int mm = elapsed / 60;
            int ss = elapsed % 60;
            char tbuf[8];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", mm > 99 ? 99 : mm, ss);
            int len = strlen(tbuf);
            int tx = (MATRIX_WIDTH - len * 6) / 2;

            StopwatchState sws = stopwatch_get_state();
            if (sws == SW_PAUSED && (millis() / 500) % 2 == 0) {
                // Blink off
            } else {
                display_draw_text(tbuf, tx, 0, display_color(0, 255, 0));
            }

            display_show();
            break;
        }

        case STATE_SYSMON_DISPLAY: {
            display_set_brightness(effective_brightness());
            display_clear();

            if (!sysmon_has_data()) {
                display_draw_text("SYS..", 1, 0, display_color(100, 100, 100));
            } else {
                int value = sysmon_get_value();
                int max_val = sysmon_get_max();
                const char* label = sysmon_get_label();
                int pct = (max_val > 0) ? (value * 100 / max_val) : 0;

                // Color based on thresholds
                uint16_t color;
                if (pct >= cfg.sysmon_crit_pct) {
                    color = display_color(255, 0, 0);      // Red
                } else if (pct >= cfg.sysmon_warn_pct) {
                    color = display_color(255, 255, 0);    // Yellow
                } else {
                    color = display_color(0, 255, 0);      // Green
                }

                if (cfg.sysmon_display_mode == 1) {
                    // Bar mode
                    display_draw_bar(value, max_val, color);
                    // Label on top
                    display_draw_text(label, 1, 0, color);
                } else {
                    // Text mode: LABEL + value
                    char tbuf[10];
                    snprintf(tbuf, sizeof(tbuf), "%s%d", label, value);
                    int len = strlen(tbuf);
                    int tx = (MATRIX_WIDTH - len * 6) / 2;
                    display_draw_text(tbuf, tx, 0, color);
                }
            }

            display_show();
            break;
        }

        case STATE_COUNTDOWN_DISPLAY: {
            display_set_brightness(effective_brightness());
            display_clear();

            long secs = countdown_get_remaining_sec();

            if (secs <= 0) {
                display_draw_text("NOW!", 4, 0, display_color(0, 255, 0));
            } else if (secs < 86400) {
                // Less than 24 hours: show H:MM
                int hours = secs / 3600;
                int mins = (secs % 3600) / 60;
                char tbuf[8];
                snprintf(tbuf, sizeof(tbuf), "%d:%02d", hours, mins);
                int len = strlen(tbuf);
                int tx = (MATRIX_WIDTH - len * 6) / 2;
                display_draw_text(tbuf, tx, 0, display_color(255, 165, 0));
            } else {
                // Days
                int days = secs / 86400;
                char tbuf[8];
                snprintf(tbuf, sizeof(tbuf), "%d D", days);
                int len = strlen(tbuf);
                int tx = (MATRIX_WIDTH - len * 6) / 2;
                display_draw_text(tbuf, tx, 0, display_color(0, 255, 255));
            }

            display_show();
            break;
        }

        case STATE_TREND_DISPLAY: {
            display_set_brightness(effective_brightness());
            display_clear();

            const GlucoseReading& reading = http_get_reading();
            if (!reading.valid || reading.trend == TREND_UNKNOWN) {
                display_draw_text("---", 7, 0, display_color(100, 100, 100));
            } else {
                uint16_t tcolor = themed_glucose_color(reading.glucose, cfg);

                // Draw 5x7 trend arrow at x=1
                display_draw_trend(reading.trend, 1, 0, tcolor);

                // Draw delta number at x=8
                int delta = http_get_delta();
                char dbuf[8];
                if (delta >= 0) {
                    snprintf(dbuf, sizeof(dbuf), "+%d", delta);
                } else {
                    snprintf(dbuf, sizeof(dbuf), "%d", delta);
                }
                display_draw_text(dbuf, 8, 0, tcolor);
            }

            display_show();
            break;
        }

        case STATE_NOTIFY_DISPLAY: {
            display_set_brightness(effective_brightness());
            display_clear();

            const char* text = notify_get_text();
            bool urgent = notify_is_urgent();
            uint16_t color = urgent ? display_color(255, 0, 0) : display_color(255, 255, 255);

            int len = strlen(text);
            if (len <= 5) {
                int x = (MATRIX_WIDTH - len * 6) / 2;
                display_draw_text(text, x, 0, color);
            } else {
                // Scrolling
                int total_w = len * 6;
                int offset = (millis() / 100) % (total_w + MATRIX_WIDTH);
                display_draw_text(text, MATRIX_WIDTH - offset, 0, color);
            }

            display_show();
            break;
        }

        case STATE_MESSAGE_DISPLAY: {
            display_clear();
            int len = strlen(message_buf);
            if (len <= 5) {
                int x = (MATRIX_WIDTH - len * 6) / 2;
                display_draw_text(message_buf, x, 0, display_color(255, 255, 255));
            } else {
                int total_w = len * 6;
                int offset = (millis() / 100) % (total_w + MATRIX_WIDTH);
                display_draw_text(message_buf, MATRIX_WIDTH - offset, 0,
                                  display_color(255, 255, 255));
            }
            display_show();
            break;
        }

        case STATE_STALE_WARNING: {
            display_clear();
            display_draw_text("STALE", 4, 0, display_color(255, 255, 0));
            display_show();
            break;
        }

        case STATE_NO_DATA: {
            display_clear();
            if ((millis() / 2000) % 2 == 0) {
                display_draw_text("NO", 10, 0, display_color(255, 0, 0));
            } else {
                display_draw_text("DATA", 4, 0, display_color(255, 0, 0));
            }
            display_show();
            break;
        }

        case STATE_NO_WIFI: {
            display_clear();
            if ((millis() / 2000) % 2 == 0) {
                display_draw_text("NO", 10, 0, display_color(255, 0, 0));
            } else {
                display_draw_text("WIFI", 4, 0, display_color(255, 0, 0));
            }
            display_show();
            break;
        }

        case STATE_NO_CFG: {
            display_clear();
            display_draw_text("SETUP", 1, 0, display_color(255, 255, 255));
            display_show();
            break;
        }
    }
}

void engine_loop() {
    // Throttle rendering
    if (millis() - last_render_ms < RENDER_INTERVAL_MS) return;
    last_render_ms = millis();

    // Periodically rebuild toggle order (catches sysmon data appearing/disappearing)
    static unsigned long last_rebuild_ms = 0;
    if (millis() - last_rebuild_ms > 5000) {
        last_rebuild_ms = millis();
        engine_rebuild_toggle_order();
    }

    // Auto-cycle display modes
    AppConfig& cfg = config_get();
    if (cfg.auto_cycle_enabled && toggle_count > 1) {
        unsigned long cycle_interval_ms = (unsigned long)cfg.auto_cycle_sec * 1000UL;
        if (last_cycle_ms == 0) last_cycle_ms = millis();
        if (millis() - last_cycle_ms >= cycle_interval_ms) {
            last_cycle_ms = millis();
            toggle_index = (toggle_index + 1) % toggle_count;
            user_mode = toggle_order[toggle_index];
            Serial.printf("[ENGINE] Auto-cycle to %s\n", engine_state_name(user_mode));
        }
    }

    DisplayState new_state = evaluate_state();
    if (new_state != current_state) {
        Serial.printf("[ENGINE] State: %s -> %s\n",
                      engine_state_name(current_state),
                      engine_state_name(new_state));
        current_state = new_state;
    }

    render_state(current_state);

    // Check buzzer alerts (non-blocking)
    check_alerts();
}

DisplayState engine_get_state() {
    return current_state;
}

DisplayState engine_get_user_mode() {
    return user_mode;
}

const char* engine_state_name(DisplayState state) {
    switch (state) {
        case STATE_BOOT:              return "BOOT";
        case STATE_GLUCOSE_DISPLAY:   return "GLUCOSE";
        case STATE_TIME_DISPLAY:      return "TIME";
        case STATE_WEATHER_DISPLAY:   return "WEATHER";
        case STATE_TIMER_DISPLAY:     return "TIMER";
        case STATE_STOPWATCH_DISPLAY: return "STOPWATCH";
        case STATE_SYSMON_DISPLAY:    return "SYSMON";
        case STATE_COUNTDOWN_DISPLAY: return "COUNTDOWN";
        case STATE_TREND_DISPLAY:     return "TREND";
        case STATE_MESSAGE_DISPLAY:   return "MESSAGE";
        case STATE_NOTIFY_DISPLAY:    return "NOTIFY";
        case STATE_STALE_WARNING:     return "STALE";
        case STATE_NO_DATA:           return "NO_DATA";
        case STATE_NO_WIFI:           return "NO_WIFI";
        case STATE_NO_CFG:            return "NO_CFG";
        default:                      return "UNKNOWN";
    }
}

void engine_force_state(DisplayState state) {
    forced_state = state;
    state_forced = true;
}

void engine_clear_force() {
    state_forced = false;
}

void engine_set_message(const char* msg) {
    strncpy(message_buf, msg, sizeof(message_buf) - 1);
    message_buf[sizeof(message_buf) - 1] = '\0';
}

void engine_set_default_mode(DisplayState mode) {
    default_mode = mode;
    user_mode = mode;
}

void engine_toggle_mode() {
    toggle_index = (toggle_index + 1) % toggle_count;
    user_mode = toggle_order[toggle_index];
    last_cycle_ms = millis(); // reset auto-cycle timer on manual toggle
    Serial.printf("[ENGINE] Toggled to %s\n", engine_state_name(user_mode));
}

void engine_toggle_mode_prev() {
    toggle_index = (toggle_index - 1 + toggle_count) % toggle_count;
    user_mode = toggle_order[toggle_index];
    last_cycle_ms = millis(); // reset auto-cycle timer on manual toggle
    Serial.printf("[ENGINE] Toggled prev to %s\n", engine_state_name(user_mode));
}

void engine_reset_auto_cycle() {
    last_cycle_ms = millis();
}

void engine_right_button_action() {
    switch (user_mode) {
        case STATE_TIMER_DISPLAY:
            timer_toggle_start_pause();
            break;
        case STATE_STOPWATCH_DISPLAY:
            stopwatch_toggle_start_pause();
            break;
        default:
            // Navigate backwards through screens
            engine_toggle_mode_prev();
            break;
    }
}

void engine_right_long_action() {
    switch (user_mode) {
        case STATE_TIMER_DISPLAY:
            timer_reset();
            break;
        case STATE_STOPWATCH_DISPLAY:
            stopwatch_reset();
            break;
        default:
            // Default: clear overrides
            engine_clear_force();
            engine_set_default_mode(STATE_GLUCOSE_DISPLAY);
            Serial.println("[ENGINE] Overrides cleared");
            break;
    }
}
