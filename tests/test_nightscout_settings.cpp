#include "nightscout_settings.h"
#include "config_request_body.h"
#include <cassert>
#include <string>

static bool apply(const char* json, NightscoutConfig& config, bool active = true) {
    JsonDocument doc;
    assert(!deserializeJson(doc, json));
    char error[160];
    return nightscout_apply_settings(doc.as<JsonObjectConst>(), config, active, error, sizeof(error));
}

int main() {
    ConfigRequestBody first = {}, second = {};
    assert(first.append("abc", 3, 0, 6));
    assert(second.append("xyz", 3, 0, 6));
    assert(first.append("def", 3, 3, 6));
    assert(second.append("uvw", 3, 3, 6));
    assert(!strcmp(first.data, "abcdef") && !strcmp(second.data, "xyzuvw"));
    ConfigRequestBody malformed = {};
    assert(!malformed.append("x", 1, 1, 2));
    assert(!malformed.append("xxx", 3, 0, 2));
    assert(!malformed.append("x", 1, 0, 4096));
    assert(malformed.append("x", 1, 0, 2));
    assert(!malformed.append("y", 1, 1, 3));
    NightscoutConfig config = {};
    assert(apply("{}", config, false)); // existing non-Nightscout settings
    assert(!apply("{}", config));       // cannot activate without a site
    assert(apply(R"({"ns_url":"https://example.org/ns","ns_auth_mode":1,"ns_credential":"clock-123"})", config));
    assert(apply(R"({"ns_credential":""})", config));
    assert(!strcmp(config.credential, "clock-123"));
    assert(apply(R"({"ns_credential":"replacement"})", config));
    assert(!strcmp(config.credential, "replacement"));
    NightscoutConfig before = config;
    assert(!apply(R"({"ns_url":"http://example.org","ns_credential":"bad"})", config));
    assert(!memcmp(&config, &before, sizeof(config))); // rejected edits are atomic
    assert(!apply(R"({"ns_auth_mode":9})", config));
    assert(!apply(R"({"ns_auth_mode":"1"})", config));
    assert(!apply(R"({"ns_url":false})", config));
    assert(!apply(R"({"ns_credential":false})", config));
    assert(!apply(R"({"ns_clear_credential":"true"})", config));
    assert(!apply(R"({"ns_credential":"bad\r\nHeader: value"})", config));
    assert(!apply(R"({"ns_credential":"new","ns_clear_credential":true})", config));
    assert(!apply(("{\"ns_credential\":\"" + std::string(256, 'x') + "\"}").c_str(), config));
    assert(apply(R"({"ns_clear_credential":true})", config));
    assert(!config.credential[0]); // removal works even for an active private site
    char error[160];
    assert(!nightscout_validate_config(config, error, sizeof(error))); // fetch explains missing auth
    assert(apply(R"({"ns_credential":"clock-123"})", config));
    assert(apply(R"({"ns_auth_mode":2})", config));
    assert(!config.credential[0]); // a token must never be reused as an API secret
    assert(apply(R"({"ns_credential":"a-private-secret"})", config));
    assert(apply(R"({"ns_auth_mode":0})", config));
    assert(!config.credential[0]);
    assert(nightscout_validate_config(config, error, sizeof(error)));
}
