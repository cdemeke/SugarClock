#pragma once
#include "config_manager.h"
#include <cstring>

inline bool cgm_provider_valid(int provider) {
    return provider == CGM_CUSTOM || provider == CGM_DEXCOM ||
           provider == CGM_LIBRE || provider == CGM_NIGHTSCOUT;
}

inline const char* cgm_provider_name(int provider) {
    switch (provider) {
        case CGM_CUSTOM: return "custom";
        case CGM_DEXCOM: return "dexcom";
        case CGM_LIBRE: return "libre";
        case CGM_NIGHTSCOUT: return "nightscout";
        default: return "none";
    }
}

inline bool cgm_uses_login(int provider) {
    return provider == CGM_DEXCOM || provider == CGM_LIBRE;
}

inline bool cgm_uses_url(int provider) {
    return provider == CGM_CUSTOM || provider == CGM_NIGHTSCOUT;
}

inline bool cgm_account_configured(const CgmAccount& a) {
    if (cgm_uses_login(a.provider)) return a.username[0] && a.password[0];
    if (cgm_uses_url(a.provider)) return a.url[0] != '\0';
    return false;
}

inline CgmAccount cgm_empty_account(int provider = CGM_NONE) {
    CgmAccount a = {};
    a.provider = provider;
    a.us = true;
    return a;
}

// Changing any of these discards readings, history and sessions. Labels and
// Libre discovery (region, display name) are presentation, not identity.
inline bool cgm_identity_equal(const CgmAccount& a, const CgmAccount& b) {
    if (a.provider != b.provider) return false;
    if (cgm_uses_login(a.provider) &&
        (strcmp(a.username, b.username) != 0 || strcmp(a.password, b.password) != 0)) return false;
    if (a.provider == CGM_DEXCOM && a.us != b.us) return false;
    if (a.provider == CGM_LIBRE && strcmp(a.patient_id, b.patient_id) != 0) return false;
    if (cgm_uses_url(a.provider) &&
        (strcmp(a.url, b.url) != 0 || strcmp(a.token, b.token) != 0)) return false;
    return true;
}

inline bool cgm_text_valid(const char* text, size_t max_length, bool printable = false) {
    if (!text || strlen(text) > max_length) return false;
    if (printable) for (const char* p = text; *p; ++p) if (*p < 32 || *p > 126) return false;
    return true;
}

inline bool cgm_url_valid(const char* url) {
    if (!cgm_text_valid(url, sizeof(CgmAccount::url) - 1, true) || strchr(url, ' ')) return false;
    return strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0;
}

// Values submitted for one slot; nullptr means "not supplied".
struct CgmAccountInput {
    int provider;
    const char* label;
    const char* username;
    const char* password;
    bool us;
    const char* url;
    const char* token;
    const char* patient_id;
};

// Builds a slot from submitted values. An empty password or token keeps the
// saved secret only for the same provider and the same username or URL, so a
// secret can never follow a slot to a different person or server.
inline bool cgm_prepare_account(CgmAccount& out, const CgmAccount& saved, const CgmAccountInput& in) {
    if (!cgm_provider_valid(in.provider)) return false;
    const char* label = in.label ? in.label : "";
    const char* username = in.username ? in.username : "";
    const char* password = in.password ? in.password : "";
    const char* url = in.url ? in.url : "";
    const char* token = in.token ? in.token : "";
    const char* patient = in.patient_id ? in.patient_id : "";
    if (!cgm_text_valid(label, sizeof(out.label) - 1, true) ||
        !cgm_text_valid(username, sizeof(out.username) - 1) ||
        !cgm_text_valid(password, sizeof(out.password) - 1) ||
        !cgm_text_valid(token, sizeof(out.token) - 1, true) || strchr(token, ' ') ||
        !cgm_text_valid(patient, sizeof(out.patient_id) - 1, true)) return false;
    out = cgm_empty_account(in.provider);
    strcpy(out.label, label);
    const bool same_provider = saved.provider == in.provider;
    if (cgm_uses_login(in.provider)) {
        if (!username[0]) return false;
        const bool same_login = same_provider && strcmp(username, saved.username) == 0;
        if (!password[0]) {
            if (!same_login || !saved.password[0]) return false;
            password = saved.password;
        }
        strcpy(out.username, username);
        strcpy(out.password, password);
        if (in.provider == CGM_DEXCOM) out.us = in.us;
        if (in.provider == CGM_LIBRE && same_login) {
            // Region and people belong to the login, not to the password.
            strcpy(out.region, saved.region);
            strcpy(out.patient_id, patient);
            if (strcmp(patient, saved.patient_id) == 0) strcpy(out.patient_name, saved.patient_name);
        }
    } else {
        if (!cgm_url_valid(url)) return false;
        strcpy(out.url, url);
        if (token[0]) strcpy(out.token, token);
        else if (same_provider && strcmp(url, saved.url) == 0) strcpy(out.token, saved.token);
    }
    return true;
}
