#ifndef GLUCOSE_ENGINE_H
#define GLUCOSE_ENGINE_H

#include <stdint.h>

// Display states
enum DisplayState {
    STATE_BOOT,
    STATE_GLUCOSE_DISPLAY,
    STATE_TIME_DISPLAY,
    STATE_WEATHER_DISPLAY,
    STATE_TIMER_DISPLAY,
    STATE_STOPWATCH_DISPLAY,
    STATE_SYSMON_DISPLAY,
    STATE_COUNTDOWN_DISPLAY,
    STATE_TREND_DISPLAY,
    STATE_MESSAGE_DISPLAY,
    STATE_NOTIFY_DISPLAY,
    STATE_STALE_WARNING,
    STATE_NO_DATA,
    STATE_NO_WIFI,
    STATE_NO_CFG
};

// Glucose thresholds for color coding
struct GlucoseThresholds {
    int urgent_low;    // default 70
    int low;           // default 80
    int high;          // default 180
    int urgent_high;   // default 250
};

// Get color for glucose value based on thresholds
// Returns 16-bit RGB565 color
uint16_t glucose_color(int mg_dl, const GlucoseThresholds& thresholds);

// Initialize glucose engine
void engine_init();

// Main engine loop - evaluates state and renders display
void engine_loop();

// Get current display state
DisplayState engine_get_state();

// Get the user-selected mode (for returning after notifications)
DisplayState engine_get_user_mode();

// Get state name string
const char* engine_state_name(DisplayState state);

// Force a specific display state (for server override)
void engine_force_state(DisplayState state);

// Clear forced state
void engine_clear_force();

// Set message text for MESSAGE_DISPLAY state
void engine_set_message(const char* msg);

// Set the preferred default mode (glucose or time)
void engine_set_default_mode(DisplayState mode);

// Toggle between display modes (data-driven array)
void engine_toggle_mode();

// Toggle to previous display mode
void engine_toggle_mode_prev();

// Rebuild the toggle order array (call after config changes)
void engine_rebuild_toggle_order();

// Reset the auto-cycle timer (call on manual button press to restart countdown)
void engine_reset_auto_cycle();

// Context-sensitive right button action
void engine_right_button_action();

// Context-sensitive right long-press action
void engine_right_long_action();

// Snooze buzzer alerts (called from button press)
void engine_snooze_alerts();

#endif // GLUCOSE_ENGINE_H
