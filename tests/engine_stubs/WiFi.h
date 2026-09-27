#pragma once
struct IPAddress { int operator[](int) const { return 1; } };
struct TestWiFi { IPAddress localIP() const { return {}; } };
static TestWiFi WiFi;
