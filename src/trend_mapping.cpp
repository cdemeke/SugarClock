#include "trend_mapping.h"
#include <strings.h>

// Parse trend string to enum
TrendType parse_trend(const char* trend_str) {
    if (!trend_str) return TREND_UNKNOWN;
    if (strcasecmp(trend_str, "RisingFast") == 0 || strcasecmp(trend_str, "DoubleUp") == 0) return TREND_RISING_FAST;
    if (strcasecmp(trend_str, "Rising") == 0 || strcasecmp(trend_str, "SingleUp") == 0) return TREND_RISING;
    if (strcasecmp(trend_str, "Flat") == 0) return TREND_FLAT;
    if (strcasecmp(trend_str, "FortyFiveUp") == 0) return TREND_FORTY_FIVE_UP;
    if (strcasecmp(trend_str, "FortyFiveDown") == 0) return TREND_FORTY_FIVE_DOWN;
    if (strcasecmp(trend_str, "Falling") == 0 || strcasecmp(trend_str, "SingleDown") == 0) return TREND_FALLING;
    if (strcasecmp(trend_str, "FallingFast") == 0 || strcasecmp(trend_str, "DoubleDown") == 0) return TREND_FALLING_FAST;
    return TREND_UNKNOWN;
}

// Parse Dexcom trend number to enum
TrendType parse_trend_number(int trend) {
    switch (trend) {
        case 1: return TREND_RISING_FAST;         // DoubleUp
        case 2: return TREND_RISING;              // SingleUp
        case 3: return TREND_FORTY_FIVE_UP;       // FortyFiveUp
        case 4: return TREND_FLAT;                // Flat
        case 5: return TREND_FORTY_FIVE_DOWN;     // FortyFiveDown
        case 6: return TREND_FALLING;             // SingleDown
        case 7: return TREND_FALLING_FAST;        // DoubleDown
        default: return TREND_UNKNOWN;
    }
}
