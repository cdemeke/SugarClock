#include "nightscout_logic.h"
#include "fake_transport.h"
#include <cassert>
#include <iostream>
#include <ctime>
FakeNightscout fake;
static time_t fake_wall = 1800000000;
extern "C" time_t time(time_t* out) { if (out) *out = fake_wall; return fake_wall; }
static NightscoutConfig config() {
    NightscoutConfig cfg = {};
    strcpy(cfg.url, "https://nightscout.example"); cfg.auth_mode = 1;
    strcpy(cfg.credential, "test-readonly-token"); return cfg;
}
static void reset() {
    fake = FakeNightscout(); fake_wall = 1800000000;
    fake.body = "[{\"sgv\":110,\"date\":1800000000000,\"direction\":\"Flat\"},{\"sgv\":100,\"date\":1799999700000}]";
}
int main() {
    NightscoutConfig cfg = config(); NightscoutResult result;
    reset();
    assert(nightscout_fetch(cfg, result));
    assert(result.glucose == 110 && result.has_previous && result.previous_glucose == 100);
    assert(result.http_code == 200 && result.age_sec == 0);
    assert(fake.ca_bundle && fake.ca_count >= 100 && fake.follow_redirects == 0);
    assert(fake.connect_timeout == 5000 && fake.read_timeout == 5000 && fake.handshake_timeout == 5);
    assert(fake.http10 && fake.ended);
    assert(fake.headers["api-secret"] == cfg.credential && fake.headers["Accept"] == "application/json");
    assert(fake.url == "https://nightscout.example/api/v1/entries/sgv.json?count=3");
    reset(); cfg.auth_mode = 0;
    assert(nightscout_fetch(cfg, result) && fake.headers.count("api-secret") == 0);
    reset(); cfg.auth_mode = 2; strcpy(cfg.credential, "abc");
    assert(nightscout_fetch(cfg, result));
    assert(fake.headers["api-secret"] == "a9993e364706816aba3e25717850c26c9cd0d89d");
    assert(fake.url.find("abc") == std::string::npos);
    reset(); fake_wall = 0;
    assert(!nightscout_fetch(cfg, result) && fake.url.empty());
    reset(); fake.begin_ok = false;
    assert(!nightscout_fetch(cfg, result));
    const int errors[] = {-1, -11, 301, 302, 307, 401, 403, 404, 429, 500, 503};
    for (int code : errors) {
        reset(); fake.code = code;
        assert(!nightscout_fetch(cfg, result) && result.http_code == code && result.error[0] && fake.ended);
        assert(!strstr(result.error, cfg.credential));
    }
    reset(); fake.body = "[]";
    assert(!nightscout_fetch(cfg, result) && result.http_code == 200);
    reset(); fake.body = "<html>Proxy failure</html>";
    assert(!nightscout_fetch(cfg, result));
    reset(); fake.size = NIGHTSCOUT_MAX_BODY_BYTES + 1;
    assert(!nightscout_fetch(cfg, result) && fake.offset == 0);
    reset(); fake.body = std::string(NIGHTSCOUT_MAX_BODY_BYTES + 1, 'x');
    assert(!nightscout_fetch(cfg, result) && fake.offset == NIGHTSCOUT_MAX_BODY_BYTES);
    reset(); fake.size = static_cast<int>(fake.body.size()) + 10;
    assert(!nightscout_fetch(cfg, result) && strstr(result.error, "Incomplete"));
    reset(); fake.encoding = "chunked";
    assert(!nightscout_fetch(cfg, result));
    reset(); fake.stall = true;
    assert(!nightscout_fetch(cfg, result) && fake.clock == 5000);
    reset(); fake.byte_delay = 4000;
    assert(!nightscout_fetch(cfg, result) && fake.clock <= 15000);
    reset(); fake.header_trickle = true;
    assert(!nightscout_fetch(cfg, result) && fake.clock <= 15000 && fake.ended);
    reset(); fake.clock = UINT32_MAX - 500; fake.next_byte = fake.clock; fake.stall = true;
    assert(!nightscout_fetch(cfg, result) && fake.clock == 4499);
    reset(); fake_wall += 1201;
    assert(nightscout_fetch(cfg, result) && result.age_sec == 1201);
    reset(); assert(nightscout_fetch(cfg, result));
    std::cout << "Nightscout actual transport/auth/limits/recovery tests passed\n";
}
