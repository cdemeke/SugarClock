#include "config_manager.h"
#include <string.h>

bool config_update_libre_credentials(AppConfig& cfg, const char* email, const char* password) {
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
