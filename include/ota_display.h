#ifndef OTA_DISPLAY_H
#define OTA_DISPLAY_H

#include <stdint.h>

enum OtaDisplayPhase {
    OTA_DISPLAY_NONE,
    OTA_DISPLAY_UPDATING,
    OTA_DISPLAY_VERIFYING,
    OTA_DISPLAY_REBOOTING,
    OTA_DISPLAY_FAILED
};

// No drawing or platform dependencies. The OTA manager serializes access to
// this lifecycle between its worker and the main-loop renderer.
class OtaDisplayLifecycle {
public:
    void start() { phase_ = OTA_DISPLAY_UPDATING; }
    void verify() { phase_ = OTA_DISPLAY_VERIFYING; }

    void fail(uint32_t now) {
        // Checks, authorization deferrals and boot rollback reports never start
        // this lifecycle, so they cannot produce an installation-failure screen.
        if (phase_ != OTA_DISPLAY_UPDATING && phase_ != OTA_DISPLAY_VERIFYING) return;
        phase_ = OTA_DISPLAY_FAILED;
        started_ms_ = now;
    }

    void reboot(uint32_t now) {
        phase_ = OTA_DISPLAY_REBOOTING;
        started_ms_ = now;
        reboot_frame_shown_ = false;
    }

    OtaDisplayPhase phase(uint32_t now) {
        if (phase_ == OTA_DISPLAY_FAILED && uint32_t(now - started_ms_) >= 8000) {
            phase_ = OTA_DISPLAY_NONE;
        }
        return phase_;
    }

    void frame_shown(OtaDisplayPhase phase, uint32_t now) {
        if (phase == OTA_DISPLAY_REBOOTING && phase_ == phase && !reboot_frame_shown_) {
            reboot_frame_shown_ = true;
            reboot_frame_ms_ = now;
        }
    }

    bool reboot_ready(uint32_t now) const {
        return phase_ == OTA_DISPLAY_REBOOTING &&
            ((reboot_frame_shown_ && uint32_t(now - reboot_frame_ms_) >= 1000) ||
             uint32_t(now - started_ms_) >= 3000);
    }

private:
    OtaDisplayPhase phase_ = OTA_DISPLAY_NONE;
    uint32_t started_ms_ = 0;
    uint32_t reboot_frame_ms_ = 0;
    bool reboot_frame_shown_ = false;
};

#endif
