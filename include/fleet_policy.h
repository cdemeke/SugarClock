#ifndef FLEET_POLICY_H
#define FLEET_POLICY_H

#include <stdint.h>

// A failed management endpoint gets two bounded retries, then the circuit opens
// for an hour. Core clock traffic is not paused while the circuit is open.
uint32_t fleet_retry_delay_ms(unsigned consecutive_failures);
bool fleet_circuit_is_open(unsigned consecutive_failures);

// A contact on the target version is never enough to acknowledge an update.
enum FleetBootOutcome { FLEET_BOOT_WAIT, FLEET_BOOT_VALIDATED,
                        FLEET_BOOT_ROLLED_BACK, FLEET_BOOT_INTERRUPTED, FLEET_BOOT_DEFERRED };
FleetBootOutcome fleet_boot_outcome(bool target_version_running, bool image_valid,
                                    bool verification_pending, bool rollback_detected,
                                    bool pending_record_from_boot, bool install_authorized = true);
uint32_t fleet_checkin_seconds(uint32_t requested);
bool fleet_authorization_valid(uint32_t now, uint32_t expires_at, bool clock_available,
                               bool install_enabled, bool inside_window);

#endif
