#ifndef NIGHTSCOUT_CLIENT_H
#define NIGHTSCOUT_CLIENT_H

#include <stdint.h>
#include "trend_arrows.h"

// Credentials never form part of a URL. 0=public, 1=access token, 2=API secret.
struct NightscoutConfig {
    char url[256];
    int auth_mode;
    char credential[256];
};

struct NightscoutResult {
    int glucose;
    TrendType trend;
    uint32_t timestamp; // sensor UTC epoch seconds
    uint32_t age_sec;
    bool has_previous;
    int previous_glucose;
    uint32_t previous_timestamp;
    int http_code;
    char error[160];
};

// Fetches at most three SGVs. True includes valid-but-stale data: consumers
// must use age_sec, not fetch success, to decide freshness and alert eligibility.
bool nightscout_fetch(const NightscoutConfig& config, NightscoutResult& result);

#endif
