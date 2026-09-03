#ifndef DOUBLE_CLICK_DETECTOR_H
#define DOUBLE_CLICK_DETECTOR_H

#include <stdint.h>

enum DoubleClickResult {
    DOUBLE_CLICK_NONE,
    DOUBLE_CLICK_SINGLE,
    DOUBLE_CLICK_DOUBLE
};

// Defers a single-click result briefly so a second press can turn it into a
// double click. The caller remains responsible for long-press detection.
class DoubleClickDetector {
public:
    explicit DoubleClickDetector(uint32_t window_ms)
        : window_ms_(window_ms), first_release_ms_(0), waiting_(false), second_down_(false) {}

    DoubleClickResult on_press(uint32_t now_ms) {
        if (!waiting_) return DOUBLE_CLICK_NONE;

        if ((uint32_t)(now_ms - first_release_ms_) < window_ms_) {
            second_down_ = true;
            return DOUBLE_CLICK_NONE;
        }

        waiting_ = false;
        second_down_ = false;
        return DOUBLE_CLICK_SINGLE;
    }

    DoubleClickResult on_short_release(uint32_t now_ms) {
        if (second_down_) {
            waiting_ = false;
            second_down_ = false;
            return DOUBLE_CLICK_DOUBLE;
        }

        first_release_ms_ = now_ms;
        waiting_ = true;
        return DOUBLE_CLICK_NONE;
    }

    DoubleClickResult poll(uint32_t now_ms, bool button_pressed) {
        if (!waiting_ || second_down_ || button_pressed) return DOUBLE_CLICK_NONE;
        if ((uint32_t)(now_ms - first_release_ms_) < window_ms_) return DOUBLE_CLICK_NONE;

        waiting_ = false;
        return DOUBLE_CLICK_SINGLE;
    }

    void cancel() {
        waiting_ = false;
        second_down_ = false;
    }

private:
    uint32_t window_ms_;
    uint32_t first_release_ms_;
    bool waiting_;
    bool second_down_;
};

#endif // DOUBLE_CLICK_DETECTOR_H
