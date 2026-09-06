#pragma once
void ble_init();
void ble_loop();
void ble_pairing_window();
void ble_reset_bonds();
void ble_render();
bool ble_is_connected();

void ble_suspend_for_ota();

// Called by main-loop schedulers; release may run on the network worker.
bool ble_acquire_network();
void ble_release_network();
bool ble_network_is_busy();
