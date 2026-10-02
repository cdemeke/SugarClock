#ifndef TREND_MAPPING_H
#define TREND_MAPPING_H

#include "trend_arrows.h"

// Dexcom/Nightscout direction names and the legacy custom-URL aliases.
TrendType parse_trend(const char* trend_str);
// Dexcom Share numeric Trend IDs (not LibreLinkUp TrendArrow IDs).
TrendType parse_trend_number(int trend);

#endif
