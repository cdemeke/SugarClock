#ifndef LIBRE_SESSION_H
#define LIBRE_SESSION_H

#include <stdint.h>
#include "trend_arrows.h"

// LibreLinkUp polling policy: when to log in, when to back off, and whether a
// reading is fresh enough to show. Free of Arduino and network code so the
// whole login/read cycle can be exercised on the host with a fake transport.

#define LIBRE_MAX_READING_AGE_SEC  (10UL * 60UL)          // same window as Dexcom Share
#define LIBRE_MAX_CLOCK_SKEW_SEC   (5UL * 60UL)           // tolerated "from the future"
#define LIBRE_MIN_VALID_EPOCH      1577836800UL           // 2020-01-01
#define LIBRE_SESSION_LIFETIME_MS  (24UL * 3600000UL)     // re-login daily
#define LIBRE_BACKOFF_MIN_MS       (5UL * 60000UL)
#define LIBRE_BACKOFF_MAX_MS       (60UL * 60000UL)
#define LIBRE_MIN_REQUEST_INTERVAL_MS 15000UL
#define LIBRE_ACCOUNT_ACTION_RETRY_MS (5UL * 60000UL)
#define LIBRE_ACCOUNT_ACTION_GRACE_MS (60UL * 60000UL)

enum LibreAuthResult {
    LIBRE_AUTH_OK,
    LIBRE_AUTH_TRANSIENT,   // network/server trouble; retry at the next poll
    LIBRE_AUTH_REJECTED,    // bad credentials or rate limited
    LIBRE_AUTH_NEEDS_ACTION // terms/privacy/email action in LibreLinkUp
};

enum LibreReadResult {
    LIBRE_READ_OK,
    LIBRE_READ_TRANSIENT,     // network/server trouble; retry at the next poll
    LIBRE_READ_UNAUTHORIZED,  // token refused (401/403 or error status)
    LIBRE_READ_REJECTED,      // rate limited; keep the token but back off
    LIBRE_READ_EMPTY          // nobody shares with this account
};

struct LibreRawReading {
    int glucose;                    // mg/dL, <= 0 when missing
    int trend_arrow;                // LibreLinkUp TrendArrow (1-5), 0 when missing
    char factory_timestamp[32];     // "9/10/2026 6:40:00 PM", UTC
};

class LibreTransport {
public:
    virtual ~LibreTransport() {}
    // Obtain a new session token, replacing any previous one
    virtual LibreAuthResult login() = 0;
    // Read the latest measurement with the current token
    virtual LibreReadResult read_latest(LibreRawReading& out) = 0;
};

enum LibreFreshness {
    LIBRE_FRESH,
    LIBRE_STALE,
    LIBRE_BAD_TIMESTAMP,     // missing, unparseable, implausibly old, or in the future
    LIBRE_CLOCK_UNSYNCED     // age can't be verified without network time
};

enum LibreFetchStatus {
    LIBRE_FETCH_OK,
    LIBRE_FETCH_CLOCK_UNSYNCED,  // no request made
    LIBRE_FETCH_BACKOFF,         // no request made; see retry_in_ms
    LIBRE_FETCH_REJECTED,        // auth refused or rate limited; backoff started
    LIBRE_FETCH_NEEDS_ACTION,    // five-minute retries, escalating after an hour
    LIBRE_FETCH_TRANSIENT,
    LIBRE_FETCH_NO_DATA,
    LIBRE_FETCH_BAD_TIMESTAMP,
    LIBRE_FETCH_STALE
};

struct LibreResult {
    LibreFetchStatus status;
    int glucose;
    TrendType trend;
    uint32_t timestamp;      // reading epoch seconds (UTC)
    uint32_t age_sec;        // valid for OK and STALE
    uint32_t retry_in_ms;    // valid for BACKOFF and REJECTED
    bool account_action_required;
};

class LibreSession {
public:
    LibreSession();

    // One poll. Refuses to run until the clock is network-synced, logs in at
    // most once, and backs off repeated rejections across the whole
    // login/read cycle. The backoff resets only after an authorized read.
    LibreResult fetch(LibreTransport& transport, uint32_t now_ms,
                      bool clock_synced, uint32_t now_epoch);

    // Forget the token and any backoff only at boot or when credentials change.
    void reset();

    unsigned consecutive_rejections() const { return rejections_; }

private:
    uint32_t backoff_ms() const;
    LibreResult reject(uint32_t now_ms);
    LibreResult needs_action(uint32_t now_ms);

    bool has_token_;
    uint32_t token_ms_;
    unsigned rejections_;
    uint32_t rejected_at_ms_;
    bool attempted_;
    uint32_t attempted_at_ms_;
    bool awaiting_action_;
    uint32_t action_waited_ms_; // saturates at the grace period; safe across millis wrap
    uint32_t action_retry_ms_;
};

// Parse FactoryTimestamp to epoch seconds; 0 when malformed or out of range
uint32_t libre_parse_timestamp(const char* s);

// LibreLinkUp TrendArrow → display trend
TrendType libre_map_trend(int arrow);

// Classify a reading's age. age_sec is set for FRESH and STALE.
LibreFreshness libre_check_freshness(uint32_t reading_epoch, bool clock_synced,
                                     uint32_t now_epoch, uint32_t* age_sec);

#endif // LIBRE_SESSION_H
