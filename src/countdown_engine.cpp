#include "countdown_engine.h"
#include "config_manager.h"
#include "time_engine.h"
#include <Arduino.h>
#include <time.h>

void countdown_init() {
    // Nothing to initialize - uses config target time
}

void countdown_loop() {
    // Lightweight - just uses time comparison
}

long countdown_get_remaining_sec() {
    AppConfig& cfg = config_get();
    if (cfg.countdown_target == 0) return 0;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return 0;

    time_t now = mktime(&timeinfo);
    long diff = (long)cfg.countdown_target - (long)now;
    return diff;
}

bool countdown_is_configured() {
    return config_get().countdown_target > 0;
}
