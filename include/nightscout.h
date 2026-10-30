#pragma once
#include "trend_mapping.h"
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

// Data source ID shared with the multi-account work (CGM_NIGHTSCOUT), so a
// saved Nightscout setup keeps working when that lands.
constexpr int NIGHTSCOUT_SOURCE = 4;

// The site's base address from whatever was pasted. https:// is assumed when no
// scheme is given; a query, fragment, trailing slash or API path such as
// /api/v1/entries.json is dropped, while a base path (/nightscout) is kept.
// Returns "" for other schemes or a missing host.
inline std::string nightscout_site(const char* input) {
    std::string url(input ? input : "");
    const size_t first = url.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    url = url.substr(first, url.find_last_not_of(" \t\r\n") - first + 1);
    size_t host = url.find("://");
    if (host == std::string::npos) {
        url = "https://" + url;
        host = 5;
    }
    for (size_t i = 0; i < host; ++i) url[i] = char(tolower((unsigned char)url[i]));
    if (url.compare(0, host, "https") != 0 && url.compare(0, host, "http") != 0) return "";
    host += 3;
    const size_t cut = url.find_first_of("?#", host);
    if (cut != std::string::npos) url.erase(cut);
    for (size_t p = url.find("/api", host); p != std::string::npos; p = url.find("/api", p + 1)) {
        if (p + 4 == url.size() || url[p + 4] == '/') {
            url.erase(p);
            break;
        }
    }
    while (url.size() > host && url.back() == '/') url.pop_back();
    if (url.size() == host || url[host] == '/' || url[host] == ':') return "";
    return url;
}

// The token of a pasted share link such as https://name.example.com/?token=reader-abc.
inline std::string nightscout_url_token(const char* input) {
    const std::string url(input ? input : "");
    const size_t end = url.find('#');
    const size_t query = url.find('?');
    if (query == std::string::npos || query > end) return "";
    for (size_t p = query + 1; p < url.size() && p < end;) {
        size_t next = url.find_first_of("&#", p);
        if (next == std::string::npos) next = url.size();
        if (url.compare(p, 6, "token=") == 0) return url.substr(p + 6, next - p - 6);
        p = next + 1;
    }
    return "";
}

// Settings save: store only the site, and move a pasted share link's token into
// the token field so the secret never sits in the address shown in settings.
inline void nightscout_normalize_settings(char* url, size_t url_size, char* token, size_t token_size) {
    const std::string pasted = nightscout_url_token(url);
    if (!token[0] && !pasted.empty() && pasted.size() < token_size) strcpy(token, pasted.c_str());
    const std::string site = nightscout_site(url);
    if (!site.empty() && site.size() < url_size) strcpy(url, site.c_str());
}

// Nightscout's own API: the latest sensor glucose entries, newest first. Values
// are mg/dL even on sites that display mmol/L. A read-only access token
// (Admin Tools, "readable" role) goes in the token query parameter; public
// sites need none. Returns "" when the site address is unusable.
inline std::string nightscout_entries_url(const char* site, const char* token) {
    std::string url = nightscout_site(site);
    if (url.empty()) return url;
    url += "/api/v1/entries/sgv.json?count=3";
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
    unsigned long timestamp;   // sensor time, epoch seconds
};

// sgv values below 39 are uploader error codes, not glucose. Every real entry
// carries its sensor time in date (epoch milliseconds); an undated value could
// never become stale, so it is rejected rather than dated by the fetch.
// direction is the arrow name ("Flat", "DoubleUp", ...); older uploaders send
// only the numeric trend, which uses Dexcom's 1-7 numbering.
inline bool nightscout_entry(double sgv, const char* direction, int trend, double date_ms,
                             NightscoutEntry& out) {
    if (!(sgv >= 39 && sgv <= 1000)) return false;
    if (!(date_ms >= 946684800000.0 && date_ms < 4102444800000.0)) return false; // 2000-2100
    out.glucose = int(std::lround(sgv));
    out.trend = direction && direction[0] ? parse_trend(direction) : parse_trend_number(trend);
    out.timestamp = (unsigned long)(date_ms / 1000);
    return true;
}

// Uploaders can backfill or duplicate entries, so keep the newest valid one
// rather than trusting the first. newest.timestamp == 0 means none yet.
inline void nightscout_consider(NightscoutEntry& newest, double sgv, const char* direction,
                                int trend, double date_ms) {
    NightscoutEntry entry = {};
    if (nightscout_entry(sgv, direction, trend, date_ms, entry) && entry.timestamp > newest.timestamp) {
        newest = entry;
    }
}
