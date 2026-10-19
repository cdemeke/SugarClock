#include "cgm_accounts.h"
#include "nightscout.h"
#include <cassert>
#include <string>

static CgmAccountInput login(int provider, const char* user, const char* password, const char* label = "SAM") {
    return CgmAccountInput{provider, label, user, password, true, "", "", ""};
}

static CgmAccountInput site(int provider, const char* url, const char* token, const char* label = "") {
    return CgmAccountInput{provider, label, "", "", true, url, token, ""};
}

int main() {
    CgmAccount saved = cgm_empty_account(), result = {};
    // Logins: a password is required for a new or changed username.
    assert(!cgm_prepare_account(result, saved, login(CGM_DEXCOM, "person", "")));
    assert(cgm_prepare_account(saved, result, login(CGM_DEXCOM, "person", "secret")));
    CgmAccountInput international = login(CGM_DEXCOM, "person", "", "JAMIE");
    international.us = false;
    assert(cgm_prepare_account(result, saved, international));
    assert(strcmp(result.password, "secret") == 0 && !result.us);
    assert(!cgm_identity_equal(saved, result));
    assert(!cgm_prepare_account(result, saved, login(CGM_DEXCOM, "other", "", "OTHER")));
    assert(cgm_prepare_account(result, saved, login(CGM_DEXCOM, "other", "new-secret", "")));
    assert(strcmp(result.password, "new-secret") == 0);
    // A secret never follows a slot to another provider, even with the same name.
    assert(!cgm_prepare_account(result, saved, login(CGM_LIBRE, "person", "")));
    for (const char* label : {"TOOLONG", "line\n", "\xc3\xa9"})
        assert(!cgm_prepare_account(result, saved, login(CGM_DEXCOM, "person", "secret", label)));
    assert(!cgm_prepare_account(result, saved, login(CGM_DEXCOM, nullptr, "secret")));
    assert(!cgm_prepare_account(result, saved, login(CGM_DEXCOM, "", "secret")));
    assert(!cgm_prepare_account(result, saved, login(CGM_DEMO_SOURCE, "person", "secret")));
    assert(!cgm_prepare_account(result, saved, login(CGM_NONE, "person", "secret")));
    const std::string long_credential(64, 'a');
    assert(!cgm_prepare_account(result, saved, login(CGM_DEXCOM, long_credential.c_str(), "secret")));
    assert(!cgm_prepare_account(result, saved, login(CGM_DEXCOM, "person", long_credential.c_str())));
    assert(cgm_prepare_account(result, saved, login(CGM_DEXCOM, "person", "", "NEW")));
    assert(cgm_identity_equal(saved, result)); // a label edit preserves history/session

    // Libre: region and person stay with the login; a new login starts clean.
    CgmAccount libre = cgm_empty_account(CGM_LIBRE);
    strcpy(libre.username, "a@example.com"); strcpy(libre.password, "pw");
    strcpy(libre.region, "eu2"); strcpy(libre.patient_id, "p1"); strcpy(libre.patient_name, "Pat");
    CgmAccountInput keep = login(CGM_LIBRE, "a@example.com", "");
    keep.patient_id = "p1";
    assert(cgm_prepare_account(result, libre, keep));
    assert(strcmp(result.region, "eu2") == 0 && strcmp(result.patient_name, "Pat") == 0);
    assert(cgm_identity_equal(libre, result));
    keep.patient_id = "p2"; // another person on the same login is a new identity
    assert(cgm_prepare_account(result, libre, keep));
    assert(strcmp(result.patient_id, "p2") == 0 && !result.patient_name[0]);
    assert(!cgm_identity_equal(libre, result));
    CgmAccountInput moved = login(CGM_LIBRE, "b@example.com", "pw2");
    moved.patient_id = "p1";
    assert(cgm_prepare_account(result, libre, moved));
    assert(!result.region[0] && !result.patient_id[0]);

    // Nightscout and custom URLs: the token stays only with the same site.
    CgmAccount ns = cgm_empty_account();
    assert(!cgm_prepare_account(result, ns, site(CGM_NIGHTSCOUT, "", "")));
    assert(!cgm_prepare_account(result, ns, site(CGM_NIGHTSCOUT, "ftp://site", "")));
    assert(!cgm_prepare_account(result, ns, site(CGM_NIGHTSCOUT, "https://bad site", "")));
    assert(!cgm_prepare_account(result, ns, site(CGM_NIGHTSCOUT, "https://ns.example", "two words")));
    assert(cgm_prepare_account(ns, result, site(CGM_NIGHTSCOUT, "https://ns.example", "reader-abc123")));
    assert(cgm_account_configured(ns));
    assert(cgm_prepare_account(result, ns, site(CGM_NIGHTSCOUT, "https://ns.example", "", "MIA")));
    assert(strcmp(result.token, "reader-abc123") == 0 && cgm_identity_equal(ns, result));
    assert(cgm_prepare_account(result, ns, site(CGM_NIGHTSCOUT, "https://other.example", "")));
    assert(!result.token[0]);
    assert(cgm_prepare_account(result, ns, site(CGM_CUSTOM, "http://192.168.1.88/reading", "")));
    assert(!result.token[0] && !cgm_identity_equal(ns, result));
    assert(!cgm_account_configured(cgm_empty_account(CGM_LIBRE)));
    assert(!cgm_account_configured(cgm_empty_account()));

    // Nightscout API URL: trailing slashes trimmed, token percent-encoded.
    assert(nightscout_entries_url("https://ns.example/", "") ==
           "https://ns.example/api/v1/entries/sgv.json?count=1");
    assert(nightscout_entries_url("https://ns.example//", "reader-a_b.c~") ==
           "https://ns.example/api/v1/entries/sgv.json?count=1&token=reader-a_b.c~");
    assert(nightscout_entries_url("http://ns.local", "a&b=c") ==
           "http://ns.local/api/v1/entries/sgv.json?count=1&token=a%26b%3Dc");

    NightscoutEntry entry = {};
    assert(nightscout_entry(123, "FortyFiveUp", 0, 1767225909000.0, entry));
    assert(entry.glucose == 123 && entry.trend == TREND_FORTY_FIVE_UP && entry.timestamp == 1767225909UL);
    assert(nightscout_entry(88.6, "", 7, 0, entry)); // numeric trend only, no date
    assert(entry.glucose == 89 && entry.trend == TREND_FALLING_FAST && entry.timestamp == 0);
    assert(nightscout_entry(150, "NOT COMPUTABLE", 4, 1767225909000.0, entry));
    assert(entry.trend == TREND_UNKNOWN);
    assert(!nightscout_entry(0, "Flat", 0, 1767225909000.0, entry));
    assert(!nightscout_entry(-5, "Flat", 0, 0, entry));
    assert(!nightscout_entry(1e9, "Flat", 0, 0, entry));
}
