#include "dexcom_accounts.h"
#include <cassert>
#include <string>
int main() {
    DexcomAccount saved = {}, result = {};
    assert(!dexcom_prepare_account(result, saved, "person", "", "SAM", true));
    assert(dexcom_prepare_account(saved, result, "person", "secret", "SAM", true));
    assert(dexcom_prepare_account(result, saved, "person", "", "JAMIE", false));
    assert(strcmp(result.password, "secret") == 0 && !result.us);
    assert(!dexcom_identity_equal(saved, result));
    assert(!dexcom_prepare_account(result, saved, "other", "", "OTHER", true));
    assert(dexcom_prepare_account(result, saved, "other", "new-secret", "", true));
    assert(strcmp(result.password, "new-secret") == 0);
    for (const char* label : {"TOOLONG", "line\n", "\xc3\xa9"})
        assert(!dexcom_prepare_account(result, saved, "person", "secret", label, true));
    assert(!dexcom_prepare_account(result, saved, nullptr, "secret", "SAM", true));
    assert(!dexcom_prepare_account(result, saved, "", "secret", "SAM", true));
    const std::string long_credential(64, 'a');
    assert(!dexcom_prepare_account(result, saved, long_credential.c_str(), "secret", "SAM", true));
    assert(!dexcom_prepare_account(result, saved, "person", long_credential.c_str(), "SAM", true));
    assert(dexcom_prepare_account(result, saved, "person", "", "NEW", true));
    assert(dexcom_identity_equal(saved, result)); // a label edit preserves history/session
}
