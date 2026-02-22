#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdint.h>
#include "trend_arrows.h"

// Glucose reading from server
struct GlucoseReading {
    int glucose;                // mg/dL
    TrendType trend;            // trend arrow type
    char message[128];          // optional message from server
    int force_mode;             // -1 = no override, else DisplayState value
    unsigned long timestamp;    // server timestamp (epoch seconds)
    unsigned long received_at_ms; // millis() when received
    bool valid;                 // true if successfully parsed
};

// History entry for graph
struct GlucoseHistoryEntry {
    int glucose;                // mg/dL
    int delta;                  // change from previous reading
    unsigned long timestamp;    // millis() when recorded
};

#define GLUCOSE_HISTORY_SIZE 48

// Initialize HTTP polling client
void http_init();

// Non-blocking polling loop
void http_loop();

// Get the latest glucose reading
const GlucoseReading& http_get_reading();

// Get failure count since last success
int http_get_failure_count();

// Get last HTTP response code
int http_get_last_response_code();

// Get last raw response body (for debug)
const char* http_get_last_response_body();

// Check if we've ever received a valid reading
bool http_has_ever_received();

// Get time since last successful reading (ms)
unsigned long http_time_since_last_reading();

// Get delta from previous reading (mg/dL, positive = rising)
int http_get_delta();

// Get history buffer (returns count, fills array)
int http_get_history(GlucoseHistoryEntry* out, int max_count);

// Force an immediate glucose fetch (for testing), returns true on success
bool http_force_fetch();

#endif // HTTP_CLIENT_H
