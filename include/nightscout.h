#pragma once
#include "trend_mapping.h"
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

// Nightscout's own API: the latest sensor glucose entry. Values are mg/dL even
// on sites that display mmol/L. A read-only access token (Admin Tools) goes in
// the token query parameter; public sites need none.
inline std::string nightscout_entries_url(const char* site, const char* token) {
    std::string url(site ? site : "");
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += "/api/v1/entries/sgv.json?count=1";
    if (token && token[0]) {
        static const char hex[] = "0123456789ABCDEF";
        url += "&token=";
        for (const unsigned char* p = (const unsigned char*)token; *p; ++p) {
            if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') url += char(*p);
            else { url += '%'; url += hex[*p >> 4]; url += hex[*p & 15]; }
        }
    }
    return url;
}

struct NightscoutEntry {
    int glucose;               // mg/dL
    TrendType trend;
    unsigned long timestamp;   // epoch seconds
};

// direction is the arrow name ("Flat", "DoubleUp", ...); older uploaders send
// only the numeric trend, which uses Dexcom's 1-7 numbering.
inline bool nightscout_entry(double sgv, const char* direction, int trend, double date_ms,
                             NightscoutEntry& out) {
    if (!(sgv >= 1 && sgv <= 1000)) return false;
    out.glucose = int(std::lround(sgv));
    out.trend = direction && direction[0] ? parse_trend(direction) : parse_trend_number(trend);
    out.timestamp = date_ms > 0 && date_ms < 4102444800000.0 ? (unsigned long)(date_ms / 1000) : 0;
    return true;
}
