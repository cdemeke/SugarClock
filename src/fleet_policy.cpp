#include "fleet_policy.h"
#include <string.h>

uint32_t fleet_retry_delay_ms(unsigned consecutive_failures) {
    if (consecutive_failures == 0) return 0;
    if (consecutive_failures == 1) return 60UL * 1000UL;
    if (consecutive_failures == 2) return 5UL * 60UL * 1000UL;
    return 60UL * 60UL * 1000UL;
}

bool fleet_circuit_is_open(unsigned consecutive_failures) {
    return consecutive_failures >= 3;
}

FleetBootOutcome fleet_boot_outcome(bool target_version_running, bool image_valid,
                                    bool verification_pending, bool rollback_detected,
                                    bool pending_record_from_boot, bool install_authorized) {
    if (target_version_running)
        return image_valid && !verification_pending ? FLEET_BOOT_VALIDATED : FLEET_BOOT_WAIT;
    if (rollback_detected) return FLEET_BOOT_ROLLED_BACK;
    if (!pending_record_from_boot) return FLEET_BOOT_WAIT;
    return install_authorized ? FLEET_BOOT_INTERRUPTED : FLEET_BOOT_DEFERRED;
}

uint32_t fleet_checkin_seconds(uint32_t requested) {
    if (requested < 270) return 270;
    return requested > 330 ? 330 : requested;
}

bool fleet_authorization_valid(uint32_t now, uint32_t expires_at, bool clock_available,
                               bool install_enabled, bool inside_window) {
    return clock_available && expires_at > now && install_enabled && inside_window;
}

bool fleet_preflight_error_is_retryable(const char* error, bool install_authorized) {
    if (install_authorized || !error) return false;
    const char* retryable[] = {"wifi_unavailable", "time_unavailable", "https_begin_failed",
        "manifest_transport_failed", "manifest_read_failed", "heap_low", "task_create_failed"};
    for (const char* value : retryable) if (strcmp(error, value) == 0) return true;
    if (strncmp(error, "manifest_http_", 14) != 0 || strlen(error) != 17) return false;
    const char* status = error + 14;
    return strcmp(status, "408") == 0 || strcmp(status, "425") == 0 ||
           strcmp(status, "429") == 0 ||
           (status[0] == '5' && status[1] >= '0' && status[1] <= '9' &&
            status[2] >= '0' && status[2] <= '9');
}

const char* fleet_local_install_block(bool manual, bool automatic_enabled, bool inside_window) {
    if (manual) return nullptr;
    if (!automatic_enabled) return "auto_update_disabled";
    return inside_window ? nullptr : "outside_maintenance_window";
}
