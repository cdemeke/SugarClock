#include "ota_boot_validation.h"

// Arduino calls this hook before setup(). Its default implementation accepts a
// pending OTA image immediately, before SugarClock's health checks can run.
// Keep the image pending so ota_manager can validate it after its health window,
// or the bootloader can roll back if the application restarts before acceptance.
// C linkage is required to override Arduino's weak C implementation.
extern "C" bool verifyRollbackLater() {
    return true;
}

void ota_boot_validation_begin(OtaBootValidationState& state, uint32_t now) {
    state = {now, 0, 0, false};
}

OtaBootValidationAction ota_boot_validation_next(
    OtaBootValidationState& state, uint32_t now, bool config_loaded,
    uint32_t asset_count, uint32_t free_heap) {
    if (state.loop_count != UINT32_MAX) ++state.loop_count;
    const uint32_t elapsed = now - state.started_ms;
    if (elapsed < 15000U) return OTA_BOOT_WAIT;
    // Unsigned subtraction also handles millis() wrapping around.
    if (state.attempted && now - state.last_attempt_ms < 5000U) return OTA_BOOT_WAIT;

    const bool local_state_ok = config_loaded && asset_count > 0 && state.loop_count > 100;
    const bool heap_ok = free_heap >= 55000U;
    // A transient allocation should not reject otherwise healthy firmware.
    // Keep the original minimum observation period, but allow heap recovery
    // until 60 seconds after validation starts (not 60 seconds per dip).
    if (local_state_ok && !heap_ok && elapsed < 60000U) return OTA_BOOT_WAIT;

    state.attempted = true;
    state.last_attempt_ms = now;
    return local_state_ok && heap_ok ? OTA_BOOT_ACCEPT : OTA_BOOT_ROLLBACK;
}
