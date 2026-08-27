#ifndef FLEET_MANAGER_H
#define FLEET_MANAGER_H

// Initializes the persistent device identity and background fleet client.
// The identity lives in the dedicated "sugarfleet" NVS namespace and survives
// ordinary firmware uploads and OTA updates.
void fleet_init();

// Non-blocking scheduler. Network work runs on a low-priority FreeRTOS task.
void fleet_loop();

#endif
