#ifndef FLEET_POLICY_H
#define FLEET_POLICY_H

#include <stdint.h>

// A failed management endpoint gets two bounded retries, then the circuit opens
// for an hour. Core clock traffic is not paused while the circuit is open.
uint32_t fleet_retry_delay_ms(unsigned consecutive_failures);
bool fleet_circuit_is_open(unsigned consecutive_failures);

#endif
