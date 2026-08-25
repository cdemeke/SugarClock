# OTA implementation baseline

The feature branch started from commit `b8668e0`. The pre-OTA binaries already committed in
the installer were 1,235,184 bytes for `firmware.bin` and 2,031,616 bytes for the old
LittleFS image. The old table had one 2 MiB app slot and LittleFS at `0x210000`.

The reproducible toolchain is pinned to PlatformIO Core 6.1.19, espressif32 7.0.1,
Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7, and exact library versions in `platformio.ini`.
The resolved ESP32 SDK configuration contains `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`;
CI checks this invariant.

The final bootstrap build is 1,283,440 bytes in a 1,835,008-byte OTA slot (69.9%). Its ESP32
image checksum and appended validation hash pass `esptool image_info`. The generated LittleFS
image is exactly 458,752 bytes.

No physical device was attached during implementation, so a pre-change serial transcript and
boot-time free-heap reading were not captured. The firmware logs the version, reset reason,
running/boot partitions, rollback state, and free heap on boot. These measurements and all
power-loss/rollback acceptance checks remain part of `OTA_HARDWARE_TESTS.md`.
