#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include "trend_arrows.h"

// Glucose reading from server
struct GlucoseReading {
    int glucose;                // mg/dL
    TrendType trend;            // trend arrow type
    char message[128];          // optional message from server
    int force_mode;             // -1 = no override, else DisplayState value
    unsigned long timestamp;    // server timestamp (epoch seconds)
    // millis() time corresponding to the reading. Providers subtract sensor age,
    // so this may wrap to a pre-boot value (and zero is valid). Compute age only
    // with unsigned subtraction: millis() - received_at_ms, never by ordering.
    unsigned long received_at_ms;
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

// Clear values/history when changing the person displayed, without lifting
// the Libre account's retry limits.
void http_clear_readings();

// Non-blocking polling loop
void http_loop();

// Get the latest glucose reading
GlucoseReading http_get_reading();
bool http_account_enabled(int index);
int http_account_count();
int http_selected_account();
void http_select_account(int index);
// Returns true when the person has a user-chosen Dexcom display name.
bool http_account_label(int index, char* out, size_t size);
void http_reset_account(int index);
int http_find_alert(int low, int high, int start = 0);
bool http_test_account(int index, GlucoseReading& reading, int& code);

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

// Pause background glucose/Dexcom/custom HTTP traffic during OTA download.
void http_set_paused(bool paused);
bool http_is_paused();

#endif // HTTP_CLIENT_H
