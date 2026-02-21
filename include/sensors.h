#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

// Initialize sensor readings (LDR + Battery)
void sensors_init();

// Update sensor readings - call from loop
void sensors_loop();

// Get smoothed LDR value (0-4095)
int sensors_get_ldr();

// Get mapped brightness from LDR (0-255)
uint8_t sensors_get_auto_brightness();

// Get battery voltage (approximate)
float sensors_get_battery_voltage();

// Get battery percentage (rough estimate)
int sensors_get_battery_percent();

#endif // SENSORS_H
