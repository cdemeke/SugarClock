#include <cassert>
#include "local_request_policy.h"
using local_request::allowed;

int main() {
    const char* ip = "192.168.1.20";
    // The clock's own UI, the iOS app, the Mac installer and scripts.
    assert(allowed("192.168.1.20", nullptr, nullptr, ip));
    assert(allowed("192.168.1.20:80", "http://192.168.1.20:80", "same-origin", ip));
    assert(allowed("192.168.1.20", "http://192.168.1.20", "same-origin", ip));
    assert(allowed("192.168.4.1", "http://192.168.4.1", "same-origin", "192.168.4.1"));
    assert(allowed(nullptr, nullptr, nullptr, ip));
    assert(allowed("192.168.1.20", nullptr, "none", ip));
    // Router-assigned local names.
    for (const char* host : {"sugarclock", "sugarclock.local", "SugarClock.LAN", "clock.home.arpa", "esp32.localdomain:80"})
        assert(allowed(host, nullptr, nullptr, ip));
    // DNS rebinding: a public name or a different IP pointed at the clock.
    for (const char* host : {"attacker.example", "evil.com:80", "local.attacker.com", "10.0.0.9", "192.168.1.2", ".local"})
        assert(!allowed(host, nullptr, nullptr, ip));
    // Cross-site browser requests, including no-cors and form posts.
    assert(!allowed("192.168.1.20", "https://attacker.example", nullptr, ip));
    assert(!allowed("192.168.1.20", "http://attacker.example", nullptr, ip));
    assert(!allowed("192.168.1.20", "null", nullptr, ip));
    assert(!allowed("192.168.1.20", "http://192.168.1.20.attacker.example", nullptr, ip));
    assert(!allowed("192.168.1.20", nullptr, "cross-site", ip));
    assert(!allowed("192.168.1.20", nullptr, "same-site", ip));
    return 0;
}
