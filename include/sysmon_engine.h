#ifndef SYSMON_ENGINE_H
#define SYSMON_ENGINE_H

#include <stdint.h>

// Initialize system monitor
void sysmon_init();

// System monitor loop - checks for stale data
void sysmon_loop();

// Push new data from computer
void sysmon_push(const char* label, int value, int max_val);

// Check if we have recent data (within 30 seconds)
bool sysmon_has_data();

// Get current values
const char* sysmon_get_label();
int sysmon_get_value();
int sysmon_get_max();

#endif // SYSMON_ENGINE_H
