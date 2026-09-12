#include "nightscout_logic.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>
#include <cstdlib>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

static bool sha1(const char* data, size_t size, uint8_t out[20]) {
#ifdef __APPLE__
    return CC_SHA1(data, static_cast<CC_LONG>(size), out) != nullptr;
#else
    return SHA1(reinterpret_cast<const unsigned char*>(data), size, out) != nullptr;
#endif
}

static const uint32_t NOW = 1800000000;
static std::string entry(int glucose, uint32_t when, const char* trend = "Flat") {
    return "{\"type\":\"sgv\",\"sgv\":" + std::to_string(glucose) + ",\"date\":" +
        std::to_string(static_cast<uint64_t>(when) * 1000) + ",\"direction\":\"" + trend + "\"}";
}
static bool parse(const std::string& input, NightscoutResult& result, uint32_t now = NOW) {
    return nightscout_parse(input.data(), input.size(), now, result);
}

static void emit_result(const NightscoutResult& result) {
    JsonDocument doc;
    doc["glucose"] = result.glucose; doc["trend"] = static_cast<int>(result.trend);
    doc["timestamp"] = result.timestamp; doc["age_sec"] = result.age_sec;
    doc["has_previous"] = result.has_previous; doc["previous_glucose"] = result.previous_glucose;
    doc["previous_timestamp"] = result.previous_timestamp; doc["error"] = result.error;
    serializeJson(doc, std::cout); std::cout << '\n';
}
int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--parse")) {
        const std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        NightscoutResult result;
        bool ok = parse(input, result, static_cast<uint32_t>(strtoull(argv[2], nullptr, 10)));
        emit_result(result); return ok ? 0 : 1;
    }
    if (argc == 5 && !strcmp(argv[1], "--request")) {
        NightscoutConfig cfg = {};
        if (strlen(argv[2]) >= sizeof(cfg.url) || strlen(argv[4]) >= sizeof(cfg.credential)) return 1;
        strcpy(cfg.url, argv[2]); cfg.auth_mode = atoi(argv[3]); strcpy(cfg.credential, argv[4]);
        char url[320] = {}, header[256] = {}, error[160] = {};
        bool ok = nightscout_build_url(cfg, url, sizeof(url), error, sizeof(error)) &&
            nightscout_build_auth(cfg, header, sizeof(header), sha1, error, sizeof(error));
        JsonDocument doc; doc["url"] = url; doc["header"] = header; doc["error"] = error;
        serializeJson(doc, std::cout); std::cout << '\n'; return ok ? 0 : 1;
    }
    NightscoutConfig cfg = {};
    strcpy(cfg.url, "https://nightscout.example/prefix///");
    char url[320], header[256], error[160];
    assert(nightscout_build_url(cfg, url, sizeof(url), error, sizeof(error)));
    assert(!strcmp(url, "https://nightscout.example/prefix/api/v1/entries/sgv.json?count=3"));
    assert(nightscout_build_auth(cfg, header, sizeof(header), sha1, error, sizeof(error)) && !header[0]);
    cfg.auth_mode = 1; strcpy(cfg.credential, "readonly-a1b2c3");
    assert(nightscout_build_auth(cfg, header, sizeof(header), sha1, error, sizeof(error)));
    assert(!strcmp(header, cfg.credential));
    assert(!strstr(url, cfg.credential));
    cfg.auth_mode = 2; strcpy(cfg.credential, "abc");
    assert(nightscout_build_auth(cfg, header, sizeof(header), sha1, error, sizeof(error)));
    assert(!strcmp(header, "a9993e364706816aba3e25717850c26c9cd0d89d"));
    assert(!nightscout_build_auth(cfg, header, 40, sha1, error, sizeof(error)));
    assert(!nightscout_build_auth(cfg, header, sizeof(header), nullptr, error, sizeof(error)));
    const char* invalid_urls[] = {"", "http://example.com", "https://", "https:///foo", "https://user:pass@host", "https://host?token=a", "https://host/#x", "https://host/api/v1/entries.json", "https://host\\foo", "https://host\nInjected: true"};
    for (const char* value : invalid_urls) {
        strcpy(cfg.url, value); assert(!nightscout_validate_config(cfg, error, sizeof(error)));
    }
    strcpy(cfg.url, "https://host:443");
    cfg.auth_mode = 9; assert(!nightscout_validate_config(cfg, error, sizeof(error)));
    cfg.auth_mode = 1; cfg.credential[0] = 0; assert(!nightscout_validate_config(cfg, error, sizeof(error)));
    strcpy(cfg.credential, "hello\r\nInjected: x"); assert(!nightscout_validate_config(cfg, error, sizeof(error)));
    strcpy(cfg.credential, "test-token"); assert(nightscout_validate_config(cfg, error, sizeof(error)));
    assert(!nightscout_build_url(cfg, url, 5, error, sizeof(error)));
    memset(cfg.url, 'a', sizeof(cfg.url)); assert(!nightscout_validate_config(cfg, error, sizeof(error)));

    NightscoutResult result;
    std::string batch = "[" + entry(100, NOW - 300) + "," + entry(110, NOW) + "," + entry(90, NOW - 600) + "]";
    assert(parse(batch, result));
    assert(result.glucose == 110 && result.timestamp == NOW && result.age_sec == 0);
    assert(result.has_previous && result.previous_glucose == 100 && result.previous_timestamp == NOW - 300);
    assert(parse(batch, result, NOW + 1200) && result.age_sec == 1200);
    assert(parse(batch, result, NOW + 1201) && result.age_sec == 1201);
    assert(parse("[" + entry(110, NOW) + "," + entry(110, NOW) + "]", result) && !result.has_previous);
    assert(!parse("[" + entry(110, NOW) + "," + entry(111, NOW) + "]", result));
    assert(parse("[" + entry(110, NOW) + "," + entry(100, NOW - 601) + "]", result) && !result.has_previous);
    assert(parse("[" + entry(110, NOW) + "," + entry(100, NOW - 600) + "]", result) && result.has_previous);
    assert(!parse(batch, result, 0));
    assert(parse("[" + entry(110, NOW + 300) + "]", result) && result.age_sec == 0);
    assert(!parse("[" + entry(110, NOW + 301) + "]", result));
    const char* bad[] = {"", "[]", "{}", "[", "null", "[1]", "[{\"sgv\":110}]", "[{\"sgv\":110,\"date\":1800000000}]", "[{\"sgv\":110,\"date\":0}]", "[{\"sgv\":110,\"date\":-1}]", "[{\"sgv\":110,\"date\":18446744073709551615}]", "[{\"sgv\":110.5,\"date\":1800000000000}]", "[{\"sgv\":\"110\",\"date\":1800000000000}]", "[{\"sgv\":110,\"date\":\"1800000000000\"}]", "[{\"sgv\":true,\"date\":1800000000000}]", "[{\"sgv\":110,\"date\":1800000000000,\"type\":\"mbg\"}]"};
    for (const char* value : bad) assert(!parse(value, result));
    assert(!parse("[" + entry(0, NOW) + "]", result));
    assert(!parse("[" + entry(-1, NOW) + "]", result));
    assert(!parse("[" + entry(1001, NOW) + "]", result));
    for (int sensor_error : {5, 10, 38}) {
        assert(!parse("[" + entry(sensor_error, NOW) + "]", result));
        assert(parse("[" + entry(sensor_error, NOW) + "," + entry(110, NOW - 300) + "]", result));
        assert(result.glucose == 110 && result.timestamp == NOW - 300 && result.age_sec == 300);
        assert(!result.has_previous);
        assert(parse("[" + entry(110, NOW) + "," + entry(sensor_error, NOW - 300) + "," + entry(100, NOW - 600) + "]", result));
        assert(result.has_previous && result.previous_glucose == 100 && result.previous_timestamp == NOW - 600);
    }
    assert(parse("[" + entry(39, NOW) + "]", result) && result.glucose == 39);
    assert(parse("[" + entry(500, NOW) + "]", result) && result.glucose == 500);
    assert(parse("[{\"sgv\":110,\"date\":1800000000000}]", result) && result.trend == TREND_UNKNOWN);
    assert(parse("[{\"sgv\":110,\"date\":1800000000123,\"direction\":12}]", result) && result.trend == TREND_UNKNOWN);
    assert(parse("[{}," + entry(110, NOW) + "]", result));
    assert(!parse("[{}, {}, {}, " + entry(110, NOW) + "]", result));
    assert(!parse(std::string(NIGHTSCOUT_MAX_BODY_BYTES + 1, ' '), result));
    assert(parse("[" + entry(110, NIGHTSCOUT_MIN_EPOCH) + "]", result));
    assert(result.age_sec == NOW - NIGHTSCOUT_MIN_EPOCH);
    const char* names[] = {"DoubleUp", "SingleUp", "FortyFiveUp", "Flat", "FortyFiveDown", "SingleDown", "DoubleDown", "NONE", "NOT COMPUTABLE", "RATE OUT OF RANGE", "unexpected"};
    TrendType expected[] = {TREND_RISING_FAST, TREND_RISING, TREND_RISING, TREND_FLAT, TREND_FALLING, TREND_FALLING, TREND_FALLING_FAST, TREND_UNKNOWN, TREND_UNKNOWN, TREND_UNKNOWN, TREND_UNKNOWN};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        assert(parse("[" + entry(110, NOW, names[i]) + "]", result)); assert(result.trend == expected[i]);
    }
    std::cout << "Nightscout request/auth/parser tests passed\n";
}
