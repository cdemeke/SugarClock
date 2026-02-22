#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>

// Application configuration stored in NVS
struct AppConfig {
    // WiFi
    char wifi_ssid[64];
    char wifi_password[64];

    // Data source: 0=custom URL, 1=Dexcom Share
    int data_source;

    // Custom server
    char server_url[256];
    char auth_token[256];

    // Dexcom Share
    char dexcom_username[64];
    char dexcom_password[64];
    bool dexcom_us;            // true=US (share2), false=international (shareous1)

    int poll_interval_sec;     // default 60, min 15

    // Display
    uint8_t brightness;        // 0-255, default 40
    bool auto_brightness;      // default true
    bool show_delta;           // show delta on LED display, default false
    bool use_mmol;             // true = mmol/L, false = mg/dL, default false

    // Glucose thresholds (mg/dL)
    int thresh_urgent_low;     // default 70
    int thresh_low;            // default 80
    int thresh_high;           // default 180
    int thresh_urgent_high;    // default 250

    // Time
    char timezone[64];         // POSIX timezone string
    bool use_24h;              // default false

    // Display mode
    int default_mode;          // 0=glucose, 1=time, 2=weather

    // Alerts (buzzer on PIN 15)
    bool alert_enabled;        // default false
    int alert_low;             // beep when below this, default 70
    int alert_high;            // beep when above this, default 250
    int alert_snooze_min;      // snooze duration in minutes, default 15

    // Theme colors (stored as 24-bit RGB packed into uint32_t)
    uint32_t color_urgent_low;   // default 0xEA4335 (red)
    uint32_t color_low;          // default 0xFBBC04 (orange/yellow)
    uint32_t color_in_range;     // default 0x34A853 (green)
    uint32_t color_high;         // default 0xFBBC04 (orange/yellow)
    uint32_t color_urgent_high;  // default 0xEA4335 (red)

    // Night mode
    bool night_mode_enabled;   // default false
    int night_start_hour;      // default 22 (10 PM)
    int night_end_hour;        // default 7 (7 AM)
    uint8_t night_brightness;  // default 10

    // Data freshness
    int stale_timeout_min;     // default 20 minutes

    // Weather
    bool weather_enabled;       // default false
    char weather_api_key[48];   // OWM API key (32 hex chars + padding)
    char weather_city[64];      // "City,CC" format, default "New York,US"
    bool weather_use_f;         // true=Fahrenheit, false=Celsius, default true
    int weather_poll_min;       // update interval, default 15, min 5

    // Date display on time screen
    bool date_on_time_screen;  // default true
    int date_format;           // 0=M/DD, 1=MMMDD, 2=DD/MM, default 0

    // Pomodoro timer
    bool timer_enabled;        // default true
    int timer_work_min;        // default 25
    int timer_break_min;       // default 5
    int timer_long_break_min;  // default 15
    int timer_sessions;        // default 4
    bool timer_buzzer;         // default true

    // Stopwatch
    bool stopwatch_enabled;    // default true

    // Notifications
    bool notify_enabled;           // default true
    int notify_default_duration;   // default 60 seconds
    bool notify_allow_buzzer;      // default true

    // System monitor
    bool sysmon_enabled;       // default true
    char sysmon_label[8];      // default "CPU"
    int sysmon_display_mode;   // 0=text, 1=bar, default 0
    int sysmon_warn_pct;       // default 50
    int sysmon_crit_pct;       // default 80

    // Auto-cycle display
    bool auto_cycle_enabled;   // default true
    int auto_cycle_sec;        // default 10, min 3, max 300

    // Countdown to event
    bool countdown_enabled;    // default false
    char countdown_name[16];   // event name, default ""
    unsigned long countdown_target; // unix timestamp, default 0

    // Config validity marker
    uint32_t magic;            // 0xGLUC to verify config is initialized
};

// Initialize config manager - loads from NVS or writes defaults
void config_init();

// Save current config to NVS
void config_save();

// Reset to factory defaults
void config_reset();

// Get reference to current config (mutable)
AppConfig& config_get();

// Check if config has WiFi credentials set
bool config_has_wifi();

// Check if config has server URL set
bool config_has_server();

// Check if Dexcom Share is configured
bool config_has_dexcom();

#endif // CONFIG_MANAGER_H
