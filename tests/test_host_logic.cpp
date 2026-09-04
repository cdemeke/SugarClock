#include "ota_policy.h"
#include "fleet_policy.h"
#include "double_click_detector.h"
#include "semver.h"

#include <assert.h>
#include <string.h>

int main() {
    DoubleClickDetector single_click(350);
    assert(single_click.on_press(100) == DOUBLE_CLICK_NONE);
    assert(single_click.on_short_release(150) == DOUBLE_CLICK_NONE);
    assert(single_click.poll(499, false) == DOUBLE_CLICK_NONE);
    assert(single_click.poll(500, false) == DOUBLE_CLICK_SINGLE);

    DoubleClickDetector double_click(350);
    assert(double_click.on_short_release(1000) == DOUBLE_CLICK_NONE);
    assert(double_click.on_press(1200) == DOUBLE_CLICK_NONE);
    assert(double_click.on_short_release(1450) == DOUBLE_CLICK_DOUBLE);
    assert(double_click.poll(2000, false) == DOUBLE_CLICK_NONE);

    DoubleClickDetector late_second_click(350);
    assert(late_second_click.on_short_release(2000) == DOUBLE_CLICK_NONE);
    assert(late_second_click.on_press(2350) == DOUBLE_CLICK_SINGLE);
    assert(late_second_click.on_short_release(2400) == DOUBLE_CLICK_NONE);
    assert(late_second_click.poll(2750, false) == DOUBLE_CLICK_SINGLE);

    DoubleClickDetector long_second_press(350);
    assert(long_second_press.on_short_release(3000) == DOUBLE_CLICK_NONE);
    assert(long_second_press.on_press(3200) == DOUBLE_CLICK_NONE);
    assert(long_second_press.poll(3600, true) == DOUBLE_CLICK_NONE);
    long_second_press.cancel();
    assert(long_second_press.poll(4000, false) == DOUBLE_CLICK_NONE);

    DoubleClickDetector wrapped_clock(350);
    assert(wrapped_clock.on_short_release(0xfffffff0U) == DOUBLE_CLICK_NONE);
    assert(wrapped_clock.on_press(0x00000040U) == DOUBLE_CLICK_NONE);
    assert(wrapped_clock.on_short_release(0x00000080U) == DOUBLE_CLICK_DOUBLE);

    assert(SemVer::parse("0.2.0").valid);
    assert(SemVer::parse("10.20.300") > SemVer::parse("2.99.999"));
    assert(SemVer::parse("1.10.0") > SemVer::parse("1.9.99"));
    assert(SemVer::parse("1.0.0") == SemVer::parse("1.0.0"));
    assert(!SemVer::parse("v1.0.0").valid);
    assert(!SemVer::parse("1.0").valid);
    assert(!SemVer::parse("1.02.3").valid);
    assert(!SemVer::parse("1.2.3-beta").valid);

    OtaSafetyInputs safe = {};
    safe.wifi_connected = true;
    safe.time_available = true;
    safe.battery_percent = 80;
    safe.free_heap = 100000;
    assert(ota_safety_failure(safe) == nullptr);

    OtaSafetyInputs test = safe;
    test.battery_percent = 44;
    assert(strcmp(ota_safety_failure(test), "battery_low") == 0);
    test = safe; test.urgent_glucose = true;
    assert(strcmp(ota_safety_failure(test), "urgent_glucose") == 0);
    test = safe; test.timer_running = true;
    assert(strcmp(ota_safety_failure(test), "timer_active") == 0);
    test = safe; test.stopwatch_running = true;
    assert(strcmp(ota_safety_failure(test), "stopwatch_active") == 0);
    test = safe; test.time_available = false;
    assert(strcmp(ota_safety_failure(test), "time_unavailable") == 0);
    test = safe; test.free_heap = 74999;
    assert(strcmp(ota_safety_failure(test), "heap_low") == 0);

    assert(ota_in_install_window(3, 3));
    assert(!ota_in_install_window(4, 3));
    assert(!ota_in_install_window(-1, 3));
    assert(ota_retry_delay_ms(1) == 15UL * 60UL * 1000UL);
    assert(ota_retry_delay_ms(2) == 30UL * 60UL * 1000UL);
    assert(ota_retry_delay_ms(99) == 6UL * 60UL * 60UL * 1000UL);

    assert(fleet_retry_delay_ms(0) == 0);
    assert(fleet_retry_delay_ms(1) == 60UL * 1000UL);
    assert(fleet_retry_delay_ms(2) == 5UL * 60UL * 1000UL);
    assert(fleet_retry_delay_ms(3) == 60UL * 60UL * 1000UL);
    assert(fleet_retry_delay_ms(99) == 60UL * 60UL * 1000UL);
    assert(!fleet_circuit_is_open(2));
    assert(fleet_circuit_is_open(3));
    return 0;
}
