#ifndef LIBRE_CLIENT_H
#define LIBRE_CLIENT_H

#include <stddef.h>
#include "trend_arrows.h"
#include "libre_patient.h"

// FreeStyle Libre via the (unofficial) LibreLinkUp follower API.
// The Libre user's phone app must be sharing to a LibreLinkUp account;
// the credentials in config are that LibreLinkUp account's login.

struct LibreReading {
    int glucose;                // mg/dL
    TrendType trend;
    unsigned long timestamp;    // sensor timestamp (epoch seconds, UTC)
    unsigned long age_sec;      // how old the reading was when fetched
};

// Fetch the latest reading. Logs in (and follows region redirects) as needed.
// Returns false on any failure; see libre_last_http_code()/libre_last_message().
bool libre_fetch(LibreReading& out, bool* attempted = nullptr);

// Hostname of the LibreLinkUp API for the configured (or not-yet-detected) region
void libre_api_host(char* out, size_t n);

// Drop the cached session and login backoff (call after credentials change)
void libre_reset_session();

// Cached people from the most recent connections response for this account.
size_t libre_get_patients(LibrePatient* out, size_t capacity);

// HTTP code of the last request (-1 = connection failure)
int libre_last_http_code();

// Human-readable status of the last request (error reason on failure)
const char* libre_last_message();

#endif // LIBRE_CLIENT_H
