#ifndef BLOCKS_CLIENT_H
#define BLOCKS_CLIENT_H

#define BLOCKS_MAX_KIDS 6

struct BlocksKid {
    char name[8];        // truncated uppercase name
    int  remaining;
    int  allocation;
};

struct BlocksReading {
    BlocksKid kids[BLOCKS_MAX_KIDS];
    int  kid_count;
    unsigned long received_at_ms;
    bool valid;
};

// Initialize blocks client
void blocks_init();

// Non-blocking polling loop (fetches on the blocks_poll_min cadence)
void blocks_loop();

// Get the latest blocks reading
const BlocksReading& blocks_get_reading();

// Check if blocks data has ever been received
bool blocks_has_data();

// True if the last-received data is older than 3x the poll interval
bool blocks_is_stale();

// Force an immediate fetch (for the test endpoint), returns true on success
bool blocks_force_fetch();

// Get the last HTTP status code from a blocks fetch
int blocks_get_last_http_code();

// Get the last error/response body from a blocks fetch (for debugging)
const char* blocks_get_last_response();

// Register a callback invoked just before a blocking blocks fetch
// (used by the engine to render a clean frame before the HTTP call blocks)
typedef void (*BlocksPreFetchCallback)();
void blocks_set_pre_fetch_callback(BlocksPreFetchCallback cb);

#endif // BLOCKS_CLIENT_H
