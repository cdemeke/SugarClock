#ifndef OTA_POLICY_H
#define OTA_POLICY_H

#include <stdint.h>

struct OtaSafetyInputs {
    bool wifi_connected;
    bool time_available;
    bool setup_or_ap_active;
    bool buzzer_active;
    bool urgent_notification;
    bool urgent_glucose;
    bool timer_running;
    bool stopwatch_running;
    int battery_percent;
    uint32_t free_heap;
};

// Returns nullptr when safe, otherwise a stable concise reason string.
const char* ota_safety_failure(const OtaSafetyInputs& inputs);

// True inside the configured one-hour local installation window.
bool ota_in_install_window(int current_hour, int configured_hour);

// Strict, overflow-safe exponential retry delay (15 min to 6 h).
uint32_t ota_retry_delay_ms(unsigned failure_count);

#endif
