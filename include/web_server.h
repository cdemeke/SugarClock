#ifndef WEB_SERVER_H
#define WEB_SERVER_H

// Initialize web server and register routes
void webserver_init();

// Start serving (call after WiFi is connected)
void webserver_start();

#endif // WEB_SERVER_H
