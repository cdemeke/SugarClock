#pragma once
#include <string.h>
#include <strings.h>
#include <stddef.h>
#include <initializer_list>

// Decides whether a browser-reachable request may use the local /api. No I/O.
//
// DNS rebinding: a remote page can point its own hostname at the clock, which
// makes it same-origin with the API. The Host header still carries the
// attacker's name, so only the clock's IP address or a name that can only be
// resolved on the local network is accepted.
//
// Cross-site requests: browsers send Origin and Sec-Fetch-Site on every
// cross-origin POST, including no-cors and form posts that need no preflight.
// Scripts such as curl or home automation send neither and stay allowed.
namespace local_request {

// "192.168.1.20" or "192.168.1.20:80" -> length of the host part.
inline size_t host_length(const char* host) {
    const char* colon = strchr(host, ':');
    return colon ? size_t(colon - host) : strlen(host);
}

inline bool ends_with(const char* s, size_t n, const char* suffix) {
    size_t m = strlen(suffix);
    return n > m && !strncasecmp(s + n - m, suffix, m);
}

// Single-label names and reserved local suffixes are only resolvable inside
// the home network, so a remote attacker cannot rebind them.
inline bool local_name(const char* host, size_t n) {
    if (!n) return false;
    if (!memchr(host, '.', n)) return true;
    for (const char* suffix : {".local", ".lan", ".home", ".home.arpa", ".internal", ".localdomain"})
        if (ends_with(host, n, suffix)) return true;
    return false;
}

inline bool host_allowed(const char* host, const char* local_ip) {
    if (!host || !*host) return true;  // HTTP/1.0 clients; browsers always send Host
    size_t n = host_length(host);
    if (local_ip && *local_ip && n == strlen(local_ip) && !strncmp(host, local_ip, n)) return true;
    // Any other dotted-number host is an IP that is not this clock.
    bool numeric = n > 0;
    for (size_t i = 0; i < n; ++i) if (!(host[i] == '.' || (host[i] >= '0' && host[i] <= '9'))) numeric = false;
    return !numeric && local_name(host, n);
}

inline bool origin_allowed(const char* origin, const char* host) {
    if (!origin) return true;
    if (!host || strncmp(origin, "http://", 7)) return false;  // includes "null"
    return !strcasecmp(origin + 7, host);
}

inline bool fetch_site_allowed(const char* site) {
    return !site || !strcmp(site, "same-origin") || !strcmp(site, "none");
}

inline bool allowed(const char* host, const char* origin, const char* fetch_site, const char* local_ip) {
    return host_allowed(host, local_ip) && origin_allowed(origin, host) && fetch_site_allowed(fetch_site);
}

}  // namespace local_request
