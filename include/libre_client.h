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

// Fetch the latest reading for one account slot. Logs in (and follows region
// redirects) as needed. Slots sharing a login share its session, and a slot
// read within LIBRE_MIN_REQUEST_INTERVAL_MS of another reuses that response.
// Returns false on any failure; see libre_last_http_code()/libre_last_message().
bool libre_fetch(int slot, LibreReading& out, bool* attempted = nullptr);

// Hostname of the LibreLinkUp API for a slot's detected (or unknown) region
void libre_api_host(int slot, char* out, size_t n);

// Forget every session and backoff (boot only; changed credentials get a
// fresh login automatically)
void libre_reset_session();

// Cached people from the most recent connections response for this exact
// login (email and password).
LibrePatients libre_get_patients(const char* email, const char* password);

// HTTP code of the last request (-1 = connection failure)
int libre_last_http_code();

// Human-readable status of the last request (error reason on failure)
const char* libre_last_message();

#endif // LIBRE_CLIENT_H
