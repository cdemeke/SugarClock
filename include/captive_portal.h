#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <IPAddress.h>

class AsyncWebServer;

// Start the wildcard DNS responder for the setup AP. Every query is answered
// with `ap_ip`, which is what makes the phone's captive-portal probe land here.
void captive_portal_start(IPAddress ap_ip);

// Stop the DNS responder
void captive_portal_stop();

// Pump the DNS server; call from wifi_loop() while the portal is up
void captive_portal_loop();

// True while the DNS responder is running
bool captive_portal_active();

// Register the OS probe endpoints and the catch-all signpost handler on the
// shared web server. Call once from webserver_init().
void captive_portal_register_routes(AsyncWebServer& server);

#endif // CAPTIVE_PORTAL_H
