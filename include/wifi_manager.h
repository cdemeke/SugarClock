#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>

// Initialize WiFi connection manager
void wifi_init();

// Non-blocking WiFi loop - handles connection/reconnection
void wifi_loop();

// Check if currently connected
bool wifi_is_connected();

// Get IP address as string
const char* wifi_get_ip();

// Get RSSI (signal strength)
int wifi_get_rssi();

// Get WiFi status string
const char* wifi_get_status();

#endif // WIFI_MANAGER_H
