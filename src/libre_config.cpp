#include "config_manager.h"
#include <string.h>

std::recursive_mutex& config_libre_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

LibreConfigSnapshot::LibreConfigSnapshot(const AppConfig& cfg, int slot) : slot(slot) {
    LibreConfigLock lock(config_libre_mutex());
    const CgmAccount& a = cfg.cgm[slot];
    strcpy(email, a.username);
    strcpy(password, a.password);
    strcpy(region, a.region);
    strcpy(patient_id, a.patient_id);
    strcpy(patient_name, a.patient_name);
}

bool LibreConfigSnapshot::matches(const AppConfig& cfg) const {
    LibreConfigLock lock(config_libre_mutex());
    const CgmAccount& a = cfg.cgm[slot];
    return a.provider == CGM_LIBRE &&
           strcmp(email, a.username) == 0 &&
           strcmp(password, a.password) == 0 &&
           strcmp(region, a.region) == 0 &&
           strcmp(patient_id, a.patient_id) == 0 &&
           strcmp(patient_name, a.patient_name) == 0;
}

bool config_apply_libre_discovery(AppConfig& cfg, const LibreConfigSnapshot& expected,
                                  const char* region, const char* patient_id,
                                  const char* patient_name) {
    LibreConfigLock lock(config_libre_mutex());
    if (!expected.matches(cfg)) return false;
    CgmAccount& a = cfg.cgm[expected.slot];
    bool changed = strcmp(a.region, region) != 0 ||
                   strcmp(a.patient_id, patient_id) != 0 ||
                   strcmp(a.patient_name, patient_name) != 0;
    strcpy(a.region, region);
    strcpy(a.patient_id, patient_id);
    strcpy(a.patient_name, patient_name);
    return changed;
}
