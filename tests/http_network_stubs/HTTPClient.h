#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#define HTTP_CODE_OK 200
#define HTTP_CODE_UNAUTHORIZED 401
#define HTTP_CODE_FORBIDDEN 403
// The generic/Dexcom transport is deliberately unavailable in these tests.
// Nightscout uses the fixture transport below its actual http_client adapter.
struct HTTPClient {
    bool begin(WiFiClientSecure&, const char*) { return false; }
    void setTimeout(int) {}
    void setConnectTimeout(int) {}
    void addHeader(const char*, const char*) {}
    int POST(const String&) { return -1; }
    int GET() { return -1; }
    String getString() { return String(); }
    void end() {}
};
