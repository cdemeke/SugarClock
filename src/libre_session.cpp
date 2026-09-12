#include "libre_session.h"
#include <ctype.h>
#include <stdio.h>

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm)
static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int days_in_month(int y, int m) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return (m == 2 && leap) ? 29 : days[m - 1];
}

uint32_t libre_parse_timestamp(const char* s) {
    if (!s) return 0;
    int mo, d, y, h, mi, se;
    char ampm[3] = "";
    int consumed = 0;
    // %n records the consumed length but does not count as an assigned field:
    // six numbers, plus one optional AM/PM token, means fields is 6 or 7.
    int fields = sscanf(s, "%d/%d/%d %d:%d:%d %n%2s%n",
                        &mo, &d, &y, &h, &mi, &se, &consumed, ampm, &consumed);
    if (fields < 6) return 0;
    while (isspace((unsigned char)s[consumed])) consumed++;
    if (s[consumed] != '\0') return 0;
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) ||
        mi < 0 || mi > 59 || se < 0 || se > 59) {
        return 0;
    }

    if (fields == 7) {
        // 12-hour clock: "AM"/"PM", hour 1-12
        bool am = (ampm[0] == 'A' || ampm[0] == 'a');
        bool pm = (ampm[0] == 'P' || ampm[0] == 'p');
        if ((!am && !pm) || (ampm[1] != 'M' && ampm[1] != 'm') || h < 1 || h > 12) return 0;
        if (pm && h < 12) h += 12;
        if (am && h == 12) h = 0;
    } else if (h < 0 || h > 23) {
        return 0;
    }

    // ESP32 long is 32 bits; use a wide intermediate for dates after 2038.
    return (uint32_t)((int64_t)days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se);
}

// LibreLinkUp TrendArrow: 1=SingleDown 2=FortyFiveDown 3=Flat 4=FortyFiveUp 5=SingleUp.
// Libre has no double arrows, so its steepest arrows map to our "fast" arrows.
// These source-specific numeric IDs intentionally differ from Dexcom's named
// SingleUp/SingleDown handling in http_client.cpp; do not share that name parser.
TrendType libre_map_trend(int arrow) {
    switch (arrow) {
        case 1: return TREND_FALLING_FAST;
        case 2: return TREND_FALLING;
        case 3: return TREND_FLAT;
        case 4: return TREND_RISING;
        case 5: return TREND_RISING_FAST;
        default: return TREND_UNKNOWN;
    }
}

LibreFreshness libre_check_freshness(uint32_t reading_epoch, bool clock_synced,
                                     uint32_t now_epoch, uint32_t* age_sec) {
    if (!clock_synced) return LIBRE_CLOCK_UNSYNCED;
    if (reading_epoch < LIBRE_MIN_VALID_EPOCH) return LIBRE_BAD_TIMESTAMP;
    if (reading_epoch > now_epoch && reading_epoch - now_epoch > LIBRE_MAX_CLOCK_SKEW_SEC) {
        return LIBRE_BAD_TIMESTAMP;
    }

    uint32_t age = now_epoch > reading_epoch ? now_epoch - reading_epoch : 0;
    if (age_sec) *age_sec = age;
    return age > LIBRE_MAX_READING_AGE_SEC ? LIBRE_STALE : LIBRE_FRESH;
}

LibreSession::LibreSession() {
    reset();
}

void LibreSession::reset() {
    has_token_ = false;
    token_ms_ = 0;
    rejections_ = 0;
    rejected_at_ms_ = 0;
    attempted_ = false;
    attempted_at_ms_ = 0;
    awaiting_action_ = false;
    action_waited_ms_ = 0;
    action_retry_ms_ = LIBRE_ACCOUNT_ACTION_RETRY_MS;
}

uint32_t LibreSession::backoff_ms() const {
    if (awaiting_action_) return action_retry_ms_;
    if (rejections_ == 0) return 0;
    uint32_t ms = LIBRE_BACKOFF_MIN_MS;
    for (unsigned i = 1; i < rejections_ && ms < LIBRE_BACKOFF_MAX_MS; i++) ms *= 2;
    return ms < LIBRE_BACKOFF_MAX_MS ? ms : LIBRE_BACKOFF_MAX_MS;
}

LibreResult LibreSession::reject(uint32_t now_ms) {
    awaiting_action_ = false;
    rejections_++;
    rejected_at_ms_ = now_ms;
    LibreResult result = LibreResult();
    result.status = LIBRE_FETCH_REJECTED;
    result.trend = TREND_UNKNOWN;
    result.retry_in_ms = backoff_ms();
    return result;
}

LibreResult LibreSession::needs_action(uint32_t now_ms) {
    has_token_ = false;
    if (!awaiting_action_) {
        action_waited_ms_ = 0;
        action_retry_ms_ = LIBRE_ACCOUNT_ACTION_RETRY_MS;
    }
    awaiting_action_ = true;
    rejections_ = 0;
    rejected_at_ms_ = now_ms;
    LibreResult result = LibreResult();
    result.status = LIBRE_FETCH_NEEDS_ACTION;
    result.trend = TREND_UNKNOWN;
    result.retry_in_ms = action_retry_ms_;
    result.account_action_required = true;
    return result;
}

LibreResult LibreSession::fetch(LibreTransport& transport, uint32_t now_ms,
                                bool clock_synced, uint32_t now_epoch) {
    LibreResult result = LibreResult();
    result.trend = TREND_UNKNOWN;

    // Without network time a reading's age can't be checked (and TLS validity
    // dates can't be either), so don't send credentials at all yet.
    if (!clock_synced) {
        result.status = LIBRE_FETCH_CLOCK_UNSYNCED;
        return result;
    }

    if (rejections_ > 0 || awaiting_action_) {
        uint32_t elapsed = now_ms - rejected_at_ms_;
        uint32_t wait = backoff_ms();
        if (elapsed < wait) {
            result.status = LIBRE_FETCH_BACKOFF;
            result.retry_in_ms = wait - elapsed;
            result.account_action_required = awaiting_action_;
            return result;
        }
        if (awaiting_action_) {
            // Count time at allowed retries, saturating before addition so a
            // long-abandoned clock cannot wrap back into the grace period.
            if (elapsed >= LIBRE_ACCOUNT_ACTION_GRACE_MS - action_waited_ms_)
                action_waited_ms_ = LIBRE_ACCOUNT_ACTION_GRACE_MS;
            else action_waited_ms_ += elapsed;
            if (action_waited_ms_ >= LIBRE_ACCOUNT_ACTION_GRACE_MS) {
                action_retry_ms_ *= 2;
                if (action_retry_ms_ > LIBRE_BACKOFF_MAX_MS)
                    action_retry_ms_ = LIBRE_BACKOFF_MAX_MS;
            }
        }
    }

    // Forced tests use this same gate, including after successful requests or
    // network errors. Reserve the attempt before calling the transport.
    if (attempted_ && now_ms - attempted_at_ms_ < LIBRE_MIN_REQUEST_INTERVAL_MS) {
        result.status = LIBRE_FETCH_BACKOFF;
        result.retry_in_ms = LIBRE_MIN_REQUEST_INTERVAL_MS - (now_ms - attempted_at_ms_);
        return result;
    }
    attempted_ = true;
    attempted_at_ms_ = now_ms;
    // An allowed retry consumes the rejection window even if it times out.
    // Keep the rejection count until an authorized read proves recovery.
    if (rejections_ > 0 || awaiting_action_) rejected_at_ms_ = now_ms;

    bool logged_in_this_poll = false;
    if (!has_token_ || now_ms - token_ms_ >= LIBRE_SESSION_LIFETIME_MS) {
        has_token_ = false;
        LibreAuthResult auth = transport.login();
        if (auth == LIBRE_AUTH_NEEDS_ACTION) return needs_action(now_ms);
        if (auth == LIBRE_AUTH_REJECTED) return reject(now_ms);
        if (auth == LIBRE_AUTH_TRANSIENT) {
            result.status = LIBRE_FETCH_TRANSIENT;
            return result;
        }
        has_token_ = true;
        token_ms_ = now_ms;
        logged_in_this_poll = true;
    }

    LibreRawReading raw = LibreRawReading();
    LibreReadResult read = transport.read_latest(raw);

    // A cached token may simply have expired: allow one fresh login per poll.
    // A token refused right after logging in counts as a rejection instead.
    if (read == LIBRE_READ_UNAUTHORIZED && !logged_in_this_poll) {
        has_token_ = false;
        LibreAuthResult auth = transport.login();
        if (auth == LIBRE_AUTH_NEEDS_ACTION) return needs_action(now_ms);
        if (auth == LIBRE_AUTH_REJECTED) return reject(now_ms);
        if (auth == LIBRE_AUTH_TRANSIENT) {
            result.status = LIBRE_FETCH_TRANSIENT;
            return result;
        }
        has_token_ = true;
        token_ms_ = now_ms;
        raw = LibreRawReading();
        read = transport.read_latest(raw);
    }

    switch (read) {
        case LIBRE_READ_UNAUTHORIZED:
            has_token_ = false;
            return reject(now_ms);
        case LIBRE_READ_REJECTED:
            return reject(now_ms);
        case LIBRE_READ_TRANSIENT:
            result.status = LIBRE_FETCH_TRANSIENT;
            return result;
        case LIBRE_READ_EMPTY:
        case LIBRE_READ_OK:
            break;
    }

    // An authorized read proves the account and token work
    rejections_ = 0;
    awaiting_action_ = false;

    if (read == LIBRE_READ_EMPTY || raw.glucose <= 0) {
        result.status = LIBRE_FETCH_NO_DATA;
        return result;
    }

    result.glucose = raw.glucose;
    result.trend = libre_map_trend(raw.trend_arrow);
    result.timestamp = libre_parse_timestamp(raw.factory_timestamp);

    switch (libre_check_freshness(result.timestamp, clock_synced, now_epoch, &result.age_sec)) {
        case LIBRE_FRESH:
            result.status = LIBRE_FETCH_OK;
            break;
        case LIBRE_STALE:
            result.status = LIBRE_FETCH_STALE;
            break;
        case LIBRE_BAD_TIMESTAMP:
            result.status = LIBRE_FETCH_BAD_TIMESTAMP;
            break;
        case LIBRE_CLOCK_UNSYNCED:
            result.status = LIBRE_FETCH_CLOCK_UNSYNCED;
            break;
    }
    return result;
}
