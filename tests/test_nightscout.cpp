#include "nightscout.h"
#include "glucose_format.h"
#include "glucose_freshness.h"
#include <cassert>
#include <cstring>
#include <string>

static const double DATE_MS = 1780000000000.0; // epoch ms beyond 32-bit range

static void site_addresses() {
    const std::string site = "https://ns.example";
    // Whatever is pasted reduces to the site, keeping a base path and port.
    assert(nightscout_site("https://ns.example") == site);
    assert(nightscout_site("https://ns.example/") == site);
    assert(nightscout_site("  https://ns.example//  ") == site);
    assert(nightscout_site("ns.example") == site);
    assert(nightscout_site("Https://ns.example") == site);
    assert(nightscout_site("https://ns.example/#/") == site);
    assert(nightscout_site("https://ns.example/?token=reader-abc") == site);
    assert(nightscout_site("https://ns.example/api/v1/entries.json?count=10") == site);
    assert(nightscout_site("https://ns.example/api") == site);
    assert(nightscout_site("https://ns.example/nightscout/api/v1/") == "https://ns.example/nightscout");
    assert(nightscout_site("https://ns.example/apidocs") == "https://ns.example/apidocs");
    assert(nightscout_site("https://apis.example") == "https://apis.example");
    assert(nightscout_site("http://192.168.1.20:1337/") == "http://192.168.1.20:1337");
    assert(nightscout_site("https://ns.example:8443") == "https://ns.example:8443");
    // Unusable addresses.
    assert(nightscout_site("").empty());
    assert(nightscout_site(nullptr).empty());
    assert(nightscout_site("   ").empty());
    assert(nightscout_site("https://").empty());
    assert(nightscout_site("https:///api").empty());
    assert(nightscout_site("https://:443").empty());
    assert(nightscout_site("ftp://ns.example").empty());
    assert(nightscout_site("httpx://ns.example").empty());
}

static void request_urls() {
    // One "?", then "&"; the token is percent-encoded.
    assert(nightscout_entries_url("https://ns.example/", "") ==
           "https://ns.example/api/v1/entries/sgv.json?count=3");
    assert(nightscout_entries_url("https://ns.example", nullptr) ==
           "https://ns.example/api/v1/entries/sgv.json?count=3");
    assert(nightscout_entries_url("https://ns.example//", "reader-a_b.c~") ==
           "https://ns.example/api/v1/entries/sgv.json?count=3&token=reader-a_b.c~");
    assert(nightscout_entries_url("http://ns.local:1337", "a&b=c d") ==
           "http://ns.local:1337/api/v1/entries/sgv.json?count=3&token=a%26b%3Dc%20d");
    // A pasted query or API path never doubles up in the request.
    assert(nightscout_entries_url("https://ns.example/api/v1/entries.json?token=x", "y") ==
           "https://ns.example/api/v1/entries/sgv.json?count=3&token=y");
    assert(nightscout_entries_url("ftp://ns.example", "y").empty());
}

static void pasted_share_links() {
    assert(nightscout_url_token("https://ns.example/?token=reader-abc") == "reader-abc");
    assert(nightscout_url_token("https://ns.example/?a=1&token=reader-abc&b=2") == "reader-abc");
    assert(nightscout_url_token("https://ns.example/?token=reader-abc#/") == "reader-abc");
    assert(nightscout_url_token("https://ns.example/?mytoken=x").empty());
    assert(nightscout_url_token("https://ns.example/#?token=x").empty());
    assert(nightscout_url_token("https://ns.example/").empty());

    // The token moves out of the address; a typed token wins.
    char url[256] = "ns.example/?token=reader-abc";
    char token[256] = "";
    nightscout_normalize_settings(url, sizeof(url), token, sizeof(token));
    assert(strcmp(url, "https://ns.example") == 0);
    assert(strcmp(token, "reader-abc") == 0);
    strcpy(url, "https://ns.example/?token=old");
    strcpy(token, "typed");
    nightscout_normalize_settings(url, sizeof(url), token, sizeof(token));
    assert(strcmp(url, "https://ns.example") == 0);
    assert(strcmp(token, "typed") == 0);
    // An unusable address is left for the fetch to report.
    strcpy(url, "ftp://ns.example");
    token[0] = '\0';
    nightscout_normalize_settings(url, sizeof(url), token, sizeof(token));
    assert(strcmp(url, "ftp://ns.example") == 0);
    assert(token[0] == '\0');
}

static void entries() {
    NightscoutEntry entry = {};
    assert(nightscout_entry(123, "FortyFiveUp", 0, DATE_MS, entry));
    assert(entry.glucose == 123 && entry.trend == TREND_FORTY_FIVE_UP);
    assert(entry.timestamp == 1780000000UL);
    assert(nightscout_entry(88.6, "", 7, DATE_MS, entry)); // numeric trend only
    assert(entry.glucose == 89 && entry.trend == TREND_FALLING_FAST);
    assert(nightscout_entry(150, "NOT COMPUTABLE", 4, DATE_MS, entry));
    assert(entry.trend == TREND_UNKNOWN);
    assert(nightscout_entry(39, "Flat", 0, DATE_MS, entry)); // sensor "LOW"
    assert(nightscout_entry(401, "Flat", 0, DATE_MS, entry)); // sensor "HIGH"

    // Uploader error codes are not glucose.
    for (double code : {0.0, 1.0, 5.0, 10.0, 38.0, 38.9, -5.0, 1001.0, 1e9}) {
        assert(!nightscout_entry(code, "Flat", 0, DATE_MS, entry));
    }
    // An undated or implausibly dated value could never go stale.
    for (double date : {0.0, -1.0, 1780000000.0, 4102444800000.0}) {
        assert(!nightscout_entry(120, "Flat", 0, date, entry));
    }
}

static void newest_valid_entry() {
    NightscoutEntry newest = {};
    // Empty array.
    assert(newest.timestamp == 0);
    // Newest first, but the newest is an error code: the older value is used,
    // and keeps its own (older) sensor time.
    nightscout_consider(newest, 5, "NONE", 0, DATE_MS + 300000);
    nightscout_consider(newest, 110, "Flat", 0, DATE_MS);
    nightscout_consider(newest, 105, "Flat", 0, DATE_MS - 300000);
    assert(newest.glucose == 110 && newest.timestamp == 1780000000UL);
    // Out of order (backfill) and duplicates.
    newest = {};
    nightscout_consider(newest, 100, "Flat", 0, DATE_MS - 300000);
    nightscout_consider(newest, 120, "SingleUp", 0, DATE_MS);
    nightscout_consider(newest, 999, "Flat", 0, DATE_MS);
    assert(newest.glucose == 120 && newest.trend == TREND_RISING);
    // Nothing valid.
    newest = {};
    nightscout_consider(newest, 0, "Flat", 0, DATE_MS);
    nightscout_consider(newest, 120, "Flat", 0, 0);
    assert(newest.timestamp == 0);
}

static void staleness_follows_sensor_time() {
    NightscoutEntry entry = {};
    assert(nightscout_entry(120, "Flat", 0, DATE_MS, entry));
    const uint32_t ts = entry.timestamp;
    // Fetched ten minutes after the sensor reading: stale on arrival.
    uint32_t anchor = glucose_received_at(ts, 0, false, 0, 5000, true, ts + 600);
    assert(uint32_t(5000 - anchor) == 600000);
    assert(glucose_display_is_stale(NIGHTSCOUT_SOURCE, 5000 - anchor, 20, 0));
    // Fresh reading, then the uploader freezes: successful polls keep returning
    // the same entry, and it still goes stale.
    anchor = glucose_received_at(ts, 0, false, 0, 5000, true, ts + 30);
    assert(!glucose_display_is_stale(NIGHTSCOUT_SOURCE, 5000 - anchor, 20, 0));
    const uint32_t later = 5000 + 8 * 60000;
    anchor = glucose_received_at(ts, ts, true, anchor, later, true, ts + 30 + 480);
    assert(glucose_display_is_stale(NIGHTSCOUT_SOURCE, later - anchor, 20, 0));
}

static void display_units() {
    // sgv is always mg/dL; the reading stays mg/dL and only the text converts,
    // with the same factor Nightscout uses (18).
    NightscoutEntry entry = {};
    assert(nightscout_entry(180, "Flat", 0, DATE_MS, entry));
    assert(entry.glucose == 180);
    char text[16];
    format_glucose_value(text, sizeof(text), entry.glucose, false);
    assert(strcmp(text, "180") == 0);
    format_glucose_value(text, sizeof(text), entry.glucose, true);
    assert(strcmp(text, "10.0") == 0);
    format_glucose_value(text, sizeof(text), 70, true);
    assert(strcmp(text, "3.9") == 0);
    format_glucose_value(text, sizeof(text), 39, true);
    assert(strcmp(text, "2.2") == 0);
    format_glucose_delta(text, sizeof(text), -9, true);
    assert(strcmp(text, "-0.5") == 0);
}

int main() {
    site_addresses();
    request_urls();
    pasted_share_links();
    entries();
    newest_valid_entry();
    staleness_follows_sensor_time();
    display_units();
    return 0;
}
