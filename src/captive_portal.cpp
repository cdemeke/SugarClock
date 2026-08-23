#include "captive_portal.h"
#include "wifi_manager.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Arduino.h>

static DNSServer dns_server;
static bool dns_running = false;

// A deliberately minimal signpost, not the setup UI.
//
// iOS opens captive responses in a sheet that closes on navigation and dies
// well before a 25-second enterprise association completes, so the flow cannot
// live in there. All this page does is tell the user where to go.
static const char SIGNPOST_HTML[] PROGMEM =
"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>SugarClock Setup</title><style>"
"body{margin:0;padding:2rem 1.25rem;font:16px/1.55 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:#0f1115;color:#e8eaed}"
"h1{font-size:1.35rem;margin:0 0 .75rem}"
"p{margin:0 0 1rem;color:#b6bcc6}"
".addr{display:block;margin:1.25rem 0;padding:1rem;border-radius:12px;background:#1b1f27;"
"border:1px solid #2b313c;font-size:1.5rem;font-weight:600;text-align:center;color:#7fd1c4;"
"letter-spacing:.02em;text-decoration:none}"
"ol{margin:0;padding-left:1.2rem;color:#b6bcc6}li{margin-bottom:.5rem}"
"@media (prefers-color-scheme:light){body{background:#f6f7f9;color:#16181d}p,ol{color:#4c525c}"
".addr{background:#fff;border-color:#dcdfe4;color:#0f7a6a}}"
"</style></head><body>"
"<h1>Finish setup in your browser</h1>"
"<p>This little window can&rsquo;t stay open long enough to join a school network. "
"Open Safari or Chrome and go to:</p>"
"<a class=\"addr\" href=\"http://192.168.4.1/\">http://192.168.4.1</a>"
"<ol><li>Stay connected to <strong>SugarClock-Setup</strong>.</li>"
"<li>Open the address above in your normal browser.</li>"
"<li>Pick your network on the WiFi tab and sign in.</li></ol>"
"</body></html>";

static void send_signpost(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* r = request->beginResponse(200, "text/html", (const uint8_t*)SIGNPOST_HTML, strlen_P(SIGNPOST_HTML));
    r->addHeader("Cache-Control", "no-store");
    request->send(r);
}

void captive_portal_start(IPAddress ap_ip) {
    if (dns_running) return;
    dns_server.setErrorReplyCode(DNSReplyCode::NoError);
    if (dns_server.start(53, "*", ap_ip)) {
        dns_running = true;
        Serial.printf("[PORTAL] DNS responder up, all names -> %s\n", ap_ip.toString().c_str());
    } else {
        Serial.println("[PORTAL] DNS responder failed to start");
    }
}

void captive_portal_stop() {
    if (!dns_running) return;
    dns_server.stop();
    dns_running = false;
    Serial.println("[PORTAL] DNS responder stopped");
}

void captive_portal_loop() {
    if (dns_running) dns_server.processNextRequest();
}

bool captive_portal_active() {
    return dns_running;
}

void captive_portal_register_routes(AsyncWebServer& server) {
    // OS captive-portal probes. Each expects a specific body when the network is
    // unrestricted; serving anything else is what pops the "sign in" sheet.
    const char* probes[] = {
        "/hotspot-detect.html",   // iOS / macOS
        "/library/test/success.html",
        "/generate_204",          // Android
        "/gen_204",
        "/ncsi.txt",              // Windows
        "/connecttest.txt",
        "/redirect",
        "/success.txt",           // Firefox
        "/canonical.html"
    };
    for (const char* path : probes) {
        server.on(path, HTTP_GET, [](AsyncWebServerRequest* request) {
            send_signpost(request);
        });
    }

    // Catch-all. While the portal is up, any unknown name resolves here via the
    // wildcard DNS, so anything that is not an API call or a real asset gets the
    // signpost rather than a bare 404.
    server.onNotFound([](AsyncWebServerRequest* request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
            return;
        }
        if (wifi_is_ap_mode() && request->method() == HTTP_GET &&
            !request->url().startsWith("/api/")) {
            send_signpost(request);
            return;
        }
        request->send(404, "text/plain", "Not found");
    });
}
