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
void http_configuration_changed();

// Get the latest glucose reading
GlucoseReading http_get_reading();
bool http_is_fetching();
unsigned long http_fetch_generation();

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

// Queue an immediate glucose fetch; true means queued, read status for its result
bool http_force_fetch();

// Pause background glucose/Dexcom/custom HTTP traffic during OTA download.
void http_set_paused(bool paused);
bool http_is_paused();

#endif // HTTP_CLIENT_H
