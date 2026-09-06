#pragma once
void ble_init();
void ble_loop();
void ble_pairing_window();
void ble_reset_bonds();
void ble_render();
bool ble_is_connected();

void ble_suspend_for_ota();
