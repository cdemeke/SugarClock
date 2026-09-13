#ifndef FLEET_MANAGER_H
#define FLEET_MANAGER_H

// Initializes the persistent device identity and background fleet client.
// The identity lives in the dedicated "sugarfleet" NVS namespace and survives
// ordinary firmware uploads and OTA updates.
void fleet_init();

// Non-blocking scheduler. Network work runs on a low-priority FreeRTOS task.
void fleet_loop();

// Public local identifier only; never expose the device credential.
const char* fleet_installation_id();
bool fleet_request_check();
bool fleet_update_authorization_current();
void fleet_record_update_outcome(bool deferred);

// Called by the OTA worker after validation, immediately before flashing.
bool fleet_authorize_update(const char* manifest_url, const char* version,
                            const char* release_channel, const char* sha256);

#endif
