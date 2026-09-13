#include "fleet_policy.h"

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
