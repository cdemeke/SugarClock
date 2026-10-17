#pragma once
#include "config_manager.h"
#include <cstring>

inline bool dexcom_identity_equal(const DexcomAccount& a, const DexcomAccount& b) {
    return a.us == b.us && strcmp(a.username, b.username) == 0 && strcmp(a.password, b.password) == 0;
}

inline bool dexcom_text_valid(const char* text, size_t max_length, bool label = false) {
    if (!text || strlen(text) > max_length) return false;
    if (label) for (const char* p = text; *p; ++p) if (*p < 32 || *p > 126) return false;
    return true;
}

// Empty passwords retain a saved secret only for the same username/slot.
inline bool dexcom_prepare_account(DexcomAccount& out, const DexcomAccount& saved,
                                   const char* username, const char* password,
                                   const char* label, bool us) {
    if (!dexcom_text_valid(username, 63) || !username[0] ||
        !dexcom_text_valid(password, 63) || !dexcom_text_valid(label, 5, true)) return false;
    if (!password[0]) {
        if (strcmp(username, saved.username) != 0 || !saved.password[0]) return false;
        password = saved.password;
    }
    out = DexcomAccount{};
    strcpy(out.username, username);
    strcpy(out.password, password);
    strcpy(out.label, label);
    out.us = us;
    return true;
}
