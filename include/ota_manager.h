#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdint.h>

enum OtaState {
    OTA_IDLE,
    OTA_CHECKING,
    OTA_UPDATE_AVAILABLE,
    OTA_DEFERRED,
    OTA_DOWNLOADING,
    OTA_VERIFYING,
    OTA_PENDING_REBOOT,
    OTA_ERROR
};

enum OtaRequestResult {
    OTA_REQUEST_QUEUED,
    OTA_REQUEST_BUSY,
    OTA_REQUEST_UNSAFE,
    OTA_REQUEST_NO_UPDATE,
    OTA_REQUEST_INTERNAL_ERROR
};

struct OtaStatusSnapshot {
    OtaState state;
    int progress;
    uint32_t last_check;
    char current_version[24];
    char available_version[24];
    char last_error[64];
    char safety_reason[64];
    char running_partition[16];
    char boot_partition[16];
    bool auto_update_enabled;
    int auto_update_hour;
    bool pending_verification;
};

void ota_init();
void ota_loop();
OtaRequestResult ota_request_check();
OtaRequestResult ota_request_install(bool manual = true);
bool ota_is_busy();
bool ota_automatic_install_is_safe();
const char* ota_state_name(OtaState state);
void ota_get_status(OtaStatusSnapshot& output);

#endif
