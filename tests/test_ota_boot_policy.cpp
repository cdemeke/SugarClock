#include "ota_boot_validation.h"
#include <assert.h>
#include <stdint.h>

static void settle(OtaBootValidationState& state, uint32_t start = 0) {
    ota_boot_validation_begin(state, start);
    for (uint32_t i = 0; i < 101; ++i)
        assert(ota_boot_validation_next(state, start + i, true, 1, 55000) == OTA_BOOT_WAIT);
}

int main() {
    OtaBootValidationState state;
    settle(state);
    assert(ota_boot_validation_next(state, 14999, true, 1, 55000) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, 15000, true, 1, 55000) == OTA_BOOT_ACCEPT);

    // A failed acceptance call cannot cause another attempt on every loop.
    for (uint32_t now = 15001; now < 20000; ++now)
        assert(ota_boot_validation_next(state, now, true, 1, 55000) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, 20000, true, 1, 55000) == OTA_BOOT_ACCEPT);
    assert(ota_boot_validation_next(state, 24999, false, 0, 0) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, 25000, false, 0, 0) == OTA_BOOT_ROLLBACK);

    // Brief low heap is recoverable without delaying all healthy boots to 60s.
    settle(state);
    assert(ota_boot_validation_next(state, 15000, true, 1, 54999) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, 15100, true, 1, 55000) == OTA_BOOT_ACCEPT);

    // Persistent low heap has a fixed deadline, not a sliding deadline per dip.
    settle(state);
    for (uint32_t now = 15000; now < 60000; now += 100)
        assert(ota_boot_validation_next(state, now, true, 1, 54999) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, 60000, true, 1, 54999) == OTA_BOOT_ROLLBACK);
    // A rollback call returning without reboot (no fallback) is also throttled.
    for (uint32_t now = 60001; now < 65000; ++now)
        assert(ota_boot_validation_next(state, now, true, 1, 54999) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, 65000, true, 1, 54999) == OTA_BOOT_ROLLBACK);
    assert(ota_boot_validation_next(state, 70000, true, 1, 55000) == OTA_BOOT_ACCEPT);

    // Recovery at the deadline still permits acceptance; no unconditional rollback.
    settle(state);
    assert(ota_boot_validation_next(state, 59999, true, 1, 1) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, 60000, true, 1, 55000) == OTA_BOOT_ACCEPT);

    // Heap grace does not hide missing local prerequisites.
    settle(state);
    assert(ota_boot_validation_next(state, 15000, false, 1, 54999) == OTA_BOOT_ROLLBACK);
    settle(state);
    assert(ota_boot_validation_next(state, 15000, true, 0, 55000) == OTA_BOOT_ROLLBACK);
    ota_boot_validation_begin(state, 0);
    for (unsigned i = 0; i < 99; ++i)
        assert(ota_boot_validation_next(state, i, true, 1, 55000) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, 15000, true, 1, 55000) == OTA_BOOT_ROLLBACK);

    // Time and loop counters remain safe across wraparound and long retries.
    const uint32_t start = UINT32_MAX - 10000U;
    settle(state, start);
    assert(ota_boot_validation_next(state, start + 14999U, true, 1, 55000) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, start + 15000U, true, 1, 55000) == OTA_BOOT_ACCEPT);
    assert(ota_boot_validation_next(state, start + 19999U, true, 1, 55000) == OTA_BOOT_WAIT);
    assert(ota_boot_validation_next(state, start + 20000U, true, 1, 55000) == OTA_BOOT_ACCEPT);
    state.loop_count = UINT32_MAX;
    assert(ota_boot_validation_next(state, start + 25000U, true, 1, 55000) == OTA_BOOT_ACCEPT);
    assert(state.loop_count == UINT32_MAX);
    settle(state); // begin() must also clear the previous attempt's retry window.
    assert(ota_boot_validation_next(state, 15000, true, 1, 55000) == OTA_BOOT_ACCEPT);
}
