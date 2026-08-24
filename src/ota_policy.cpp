#include "ota_policy.h"

const char* ota_safety_failure(const OtaSafetyInputs& i) {
    if (!i.wifi_connected) return "wifi_unavailable";
    if (!i.time_available) return "time_unavailable";
    if (i.setup_or_ap_active) return "setup_active";
    if (i.battery_percent >= 0 && i.battery_percent < 45) return "battery_low";
    if (i.buzzer_active) return "buzzer_active";
    if (i.urgent_notification) return "urgent_notification";
    if (i.urgent_glucose) return "urgent_glucose";
    if (i.timer_running) return "timer_active";
    if (i.stopwatch_running) return "stopwatch_active";
    if (i.free_heap < 75000U) return "heap_low";
    return nullptr;
}

bool ota_in_install_window(int current_hour, int configured_hour) {
    if (current_hour < 0 || current_hour > 23 ||
        configured_hour < 0 || configured_hour > 23) return false;
    return current_hour == configured_hour;
}

uint32_t ota_retry_delay_ms(unsigned failure_count) {
    const uint32_t base = 15UL * 60UL * 1000UL;
    const uint32_t max_delay = 6UL * 60UL * 60UL * 1000UL;
    unsigned shift = failure_count > 1 ? failure_count - 1 : 0;
    if (shift > 5) shift = 5;
    uint32_t delay = base << shift;
    return delay > max_delay ? max_delay : delay;
}
