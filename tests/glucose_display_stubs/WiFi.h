#pragma once
struct IPAddress {
    int operator[](int) const { return 0; }
};
struct FakeWiFi {
    IPAddress localIP() const { return {}; }
};
static FakeWiFi WiFi;
