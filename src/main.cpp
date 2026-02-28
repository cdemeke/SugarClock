#include <Arduino.h>
#include <esp_task_wdt.h>
#include "hardware_pins.h"
#include "config_manager.h"
#include "display.h"
#include "glucose_engine.h"
#include "buttons.h"
#include "wifi_manager.h"
#include "time_engine.h"
#include "sensors.h"
#include "http_client.h"
#include "weather_client.h"
#include "web_server.h"
#include "buzzer.h"
#include "timer_engine.h"
#include "notify_engine.h"
#include "sysmon_engine.h"
#include "countdown_engine.h"
#include "improv_serial.h"

#define FIRMWARE_VERSION "0.1.0"
#define WDT_TIMEOUT_SEC  30

// Performance tracking
static unsigned long loop_count = 0;
static unsigned long loop_time_sum = 0;
static unsigned long loop_time_max = 0;
static unsigned long last_diag_ms = 0;
#define DIAG_INTERVAL_MS 60000  // log diagnostics every 60s

static bool webserver_started = false;

void setup() {
    // 1. Serial init
    Serial.begin(115200);
    delay(100);

    // 2. Initialize buzzer (silences immediately)
    buzzer_init();

    // Log boot reason
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.println();
    Serial.println("================================");
    Serial.printf("SugarClock v%s\n", FIRMWARE_VERSION);
    Serial.printf("Reset reason: %d\n", reason);
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        Serial.println("WARNING: Previous watchdog reset!");
    }
    Serial.println("================================");

    // 3. Load configuration
    config_init();

    // 4. Initialize display + show boot screen
    display_init();
    display_clear();
    display_draw_text("SUGAR", 1, 0, display_color(0, 200, 200));
    display_show();

    // 5. Init buttons
    buttons_init();

    // 6. Init WiFi
    wifi_init();

    // 7. Init time engine
    time_init();

    // 8. Init sensors
    sensors_init();

    // 9. Init HTTP client
    http_init();

    // 10. Init weather client
    weather_init();

    // 11. Init web server routes (doesn't start serving yet)
    webserver_init();

    // 12. Init new feature engines
    timer_init();
    notify_init();
    sysmon_init();
    countdown_init();

    // 13. Init Improv Wi-Fi serial handler
    improv_init();

    // 14. Init glucose engine (state machine)
    engine_init();

    // 14. Enable watchdog timer
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL); // add current task

    Serial.println("[BOOT] Setup complete");
    Serial.printf("[BOOT] Free heap: %d bytes\n", ESP.getFreeHeap());
}

void loop() {
    unsigned long loop_start = millis();

    // Reset watchdog
    esp_task_wdt_reset();

    // 1. WiFi management
    wifi_loop();

    // 1b. Improv Wi-Fi serial (for ESP Web Tools credential input)
    improv_loop();

    // Start web server once WiFi connects or in AP mode (one-time)
    if ((wifi_is_connected() || wifi_is_ap_mode()) && !webserver_started) {
        webserver_start();
        webserver_started = true;
    }

    // 2. HTTP polling
    http_loop();

    // 2b. Weather polling
    weather_loop();

    // 3. Time management
    time_loop();

    // 4. Button input
    buttons_loop();
    ButtonEvent evt = buttons_get_event();
    if (evt != BTN_NONE) {
        switch (evt) {
            case BTN_LEFT_SHORT:
                engine_toggle_mode();
                break;
            case BTN_MIDDLE_SHORT: {
                // Cycle brightness: 10 -> 40 -> 100 -> 200 -> 10
                AppConfig& cfg = config_get();
                if (cfg.brightness < 20) cfg.brightness = 40;
                else if (cfg.brightness < 60) cfg.brightness = 100;
                else if (cfg.brightness < 150) cfg.brightness = 200;
                else cfg.brightness = 10;
                cfg.auto_brightness = false;
                display_set_brightness(cfg.brightness);
                config_save();
                Serial.printf("[BTN] Brightness: %d\n", cfg.brightness);
                break;
            }
            case BTN_MIDDLE_LONG:
                // Snooze buzzer alerts
                engine_snooze_alerts();
                Serial.println("[BTN] Alerts snoozed");
                break;
            case BTN_RIGHT_SHORT:
                // Context-sensitive right button
                engine_right_button_action();
                break;
            case BTN_LEFT_LONG:
                engine_clear_force();
                engine_set_default_mode(STATE_GLUCOSE_DISPLAY);
                Serial.println("[BTN] Overrides cleared");
                break;
            case BTN_RIGHT_LONG:
                // Context-sensitive right long press
                engine_right_long_action();
                break;
            default:
                break;
        }
    }

    // 5. Sensor readings
    sensors_loop();

    // Apply auto-brightness if enabled
    AppConfig& cfg = config_get();
    if (cfg.auto_brightness) {
        uint8_t auto_brt = sensors_get_auto_brightness();
        display_set_brightness(auto_brt);
    }

    // 6. Feature engine loops
    buzzer_loop();
    timer_loop();
    notify_loop();
    sysmon_loop();
    countdown_loop();

    // 7. Engine state machine + rendering
    engine_loop();

    // Performance tracking
    unsigned long loop_time = millis() - loop_start;
    loop_count++;
    loop_time_sum += loop_time;
    if (loop_time > loop_time_max) loop_time_max = loop_time;

    // Periodic diagnostic logging
    if (millis() - last_diag_ms > DIAG_INTERVAL_MS) {
        last_diag_ms = millis();
        unsigned long avg = (loop_count > 0) ? (loop_time_sum / loop_count) : 0;
        Serial.printf("[DIAG] Heap: %d/%d, Loop avg: %lums, max: %lums, state: %s\n",
                      ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                      avg, loop_time_max,
                      engine_state_name(engine_get_state()));
        loop_count = 0;
        loop_time_sum = 0;
        loop_time_max = 0;
    }

    // No delay() - all subsystems use millis()-based timing
}
