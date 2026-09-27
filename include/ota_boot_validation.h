#ifndef OTA_BOOT_VALIDATION_H
#define OTA_BOOT_VALIDATION_H

#include <stdint.h>

enum OtaBootValidationAction { OTA_BOOT_WAIT, OTA_BOOT_ACCEPT, OTA_BOOT_ROLLBACK };

struct OtaBootValidationState {
    uint32_t started_ms;
    uint32_t last_attempt_ms;
    uint32_t loop_count;
    bool attempted;
};

void ota_boot_validation_begin(OtaBootValidationState& state, uint32_t now);
// Returning an action reserves an attempt, even if the subsequent SDK call fails.
OtaBootValidationAction ota_boot_validation_next(
    OtaBootValidationState& state, uint32_t now, bool config_loaded,
    uint32_t asset_count, uint32_t free_heap);

#endif
