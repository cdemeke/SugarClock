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
