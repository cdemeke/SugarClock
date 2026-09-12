#include "config_manager.h"
#include <string.h>

std::recursive_mutex& config_libre_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

LibreConfigSnapshot::LibreConfigSnapshot(const AppConfig& cfg) {
    LibreConfigLock lock(config_libre_mutex());
    strcpy(email, cfg.libre_email);
    strcpy(password, cfg.libre_password);
    strcpy(region, cfg.libre_region);
    strcpy(patient_id, cfg.libre_patient_id);
    strcpy(patient_name, cfg.libre_patient_name);
}

bool LibreConfigSnapshot::matches(const AppConfig& cfg) const {
    LibreConfigLock lock(config_libre_mutex());
    return strcmp(email, cfg.libre_email) == 0 &&
           strcmp(password, cfg.libre_password) == 0 &&
           strcmp(region, cfg.libre_region) == 0 &&
           strcmp(patient_id, cfg.libre_patient_id) == 0 &&
           strcmp(patient_name, cfg.libre_patient_name) == 0;
}

bool config_apply_libre_discovery(AppConfig& cfg, const LibreConfigSnapshot& expected,
                                  const char* region, const char* patient_id,
                                  const char* patient_name) {
    LibreConfigLock lock(config_libre_mutex());
    if (!expected.matches(cfg)) return false;
    bool changed = strcmp(cfg.libre_region, region) != 0 ||
                   strcmp(cfg.libre_patient_id, patient_id) != 0 ||
                   strcmp(cfg.libre_patient_name, patient_name) != 0;
    strcpy(cfg.libre_region, region);
    strcpy(cfg.libre_patient_id, patient_id);
    strcpy(cfg.libre_patient_name, patient_name);
    return changed;
}

bool config_update_libre_credentials(AppConfig& cfg, const char* email, const char* password) {
    LibreConfigLock lock(config_libre_mutex());
    bool changed = false;
    if (email) {
        char saved[sizeof(cfg.libre_email)] = {};
        strncpy(saved, email, sizeof(saved) - 1);
        if (strcmp(saved, cfg.libre_email) != 0) {
            strcpy(cfg.libre_email, saved);
            cfg.libre_region[0] = '\0';
            cfg.libre_patient_id[0] = '\0';
            cfg.libre_patient_name[0] = '\0';
            changed = true;
        }
    }
    if (password && password[0]) {
        char saved[sizeof(cfg.libre_password)] = {};
        strncpy(saved, password, sizeof(saved) - 1);
        if (strcmp(saved, cfg.libre_password) != 0) {
            strcpy(cfg.libre_password, saved);
            changed = true;
        }
    }
    return changed;
}
