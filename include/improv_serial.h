#ifndef IMPROV_SERIAL_H
#define IMPROV_SERIAL_H

// Initialize Improv Wi-Fi serial handler
void improv_init();

// Process incoming Improv serial data (call in loop)
void improv_loop();

// Check if Improv is currently provisioning
bool improv_is_active();

#endif // IMPROV_SERIAL_H
