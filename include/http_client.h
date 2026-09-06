#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
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

// Called from the background network task to poll glucose on schedule.
// Replaces http_loop() when the net_task is in use.
void http_poll_tick();

// Get the latest glucose reading (thread-safe copy).
GlucoseReading http_get_reading();

// Get failure count since last success
int http_get_failure_count();

// Get last HTTP response code
int http_get_last_response_code();

// Copy last raw response body into `out` (thread-safe).
void http_get_last_response_body(char* out, size_t out_len);

// Check if we've ever received a valid reading
bool http_has_ever_received();

// Get time since last successful reading (ms)
unsigned long http_time_since_last_reading();

// Get delta from previous reading (mg/dL, positive = rising)
int http_get_delta();

// Get history buffer (returns count, fills array)
int http_get_history(GlucoseHistoryEntry* out, int max_count);

// Force an immediate glucose fetch on the network task.
// Returns true on success, false on failure or timeout.
// `timeout_ms` controls how long to wait for the background task.
bool http_force_fetch(unsigned long timeout_ms);

// Pause background glucose/Dexcom/custom HTTP traffic during OTA download.
void http_set_paused(bool paused);
bool http_is_paused();

#endif // HTTP_CLIENT_H
