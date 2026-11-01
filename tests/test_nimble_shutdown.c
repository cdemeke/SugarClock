#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Model the FreeRTOS NPL contract: stopping a timer prevents future expiration,
// but does not remove an event which already entered the host's event queue.
struct event { void (*fn)(void); };
struct callout { struct event ev; bool active; };
static struct callout ble_hs_timer;
static struct event *queued;
static bool enabled;
static unsigned callbacks, deinitializations, resets;
static int ble_hs_mutex, ble_hs_rx_q;
static int ble_hs_ev_start_stage2, ble_hs_ev_start_stage1, ble_hs_ev_reset;

#define BLE_MONITOR 0
#define NIMBLE_BLE_CONNECT 0
#define MYNEWT_VAL(name) 0
#define BLE_HS_DBG_ASSERT_EVAL(value) assert(value)
static bool ble_hs_is_enabled(void) { return enabled; }
static void expired(void) { ++callbacks; }
static void ble_npl_callout_stop(struct callout *co) { co->active = false; }
static void ble_npl_callout_deinit(struct callout *co) {
    ++deinitializations;
    memset(co, 0, sizeof(*co));
}
static int ble_npl_callout_reset(struct callout *co, uint32_t ticks) {
    assert(ticks == 42);
    co->active = true;
    ++resets;
    return 0;
}
static void ble_hs_flow_deinit(void) {}
static void ble_npl_mutex_deinit(int *unused) { (void) unused; }
static void ble_mqueue_deinit(int *unused) { (void) unused; }
static void ble_hs_stop_deinit(void) {}
static void ble_gap_deinit(void) {}
static void ble_hs_hci_deinit(void) {}
static void ble_npl_event_deinit(int *unused) { (void) unused; }

#include "host.inc"

int main(void) {
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
        ble_hs_timer.ev.fn = expired;
        enabled = true;
        ble_hs_timer_reset(42);
        assert(ble_hs_timer.active);
        // Expiration occurred just before ble_hs_stop_begin sets STOPPING.
        queued = &ble_hs_timer.ev;
        enabled = false;
        ble_hs_timer_reset(0);
        assert(!ble_hs_timer.active);
        // Return a known failure instead of executing the original NULL callback.
        if (!queued->fn) return 12;
        assert(deinitializations == cycle);
        queued->fn();
        queued = 0;
        // The real final host teardown must still release this resource.
        ble_hs_deinit();
        assert(ble_hs_timer.ev.fn == 0);
        assert(deinitializations == cycle + 1);
    }
    assert(callbacks == 1000);
    assert(resets == 1000);
    return 0;
}
