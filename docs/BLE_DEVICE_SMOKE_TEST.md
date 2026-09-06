# Connected TC001 smoke test — 2026-09-06

The owner explicitly authorized flashing the connected clock for app testing. This is a development build and a limited physical smoke test, not BLE/security or release qualification.

## Image and preservation

- Hardware identified over USB: ESP32-D0WD revision 1.1, 4 MiB flash, TC001.
- Firmware: 0.3.0, protocol 1, existing pinned toolchain and NimBLE-Arduino 2.5.0.
- Final binary: **1,585,760 bytes**, SHA-256 `00ae90277314970d63939cf5e45bd71122a027c0fc4eff22907cb91efade9b75`.
- ELF flash: 1,579,189 bytes; static RAM: 93,512 bytes. Each existing OTA slot remains 1,835,008 bytes, leaving **249,248 bytes** of binary headroom.
- A private full-flash backup was taken before modification. Its NVS and filesystem contents contain secrets and are deliberately excluded from the repository and diagnostics.
- The clock already had the compatible partition layout. Only the application at `0x10000` was written and verified by esptool. Bootloader, partition table, OTA metadata, NVS and LittleFS were not written or erased.
- The partition table and LittleFS matched the backup. No pending filesystem configuration overlay was present. After runtime initialization, all **79 application-configuration NVS entries** matched; the only changed namespace was the new NimBLE bond storage. This is not a legacy-layout migration test.

## Failure found and corrected

The original candidate booted and reconnected to Wi-Fi, but Dexcom authentication failed with a TLS BIGNUM allocation error while BLE occupied memory. The observed minimum free heap reached 6,164 bytes. This was a physical-device finding that compilation and simulator tests did not reveal.

Scheduled glucose, fleet, weather and diagnostic TLS work now takes a serialized network lease. BLE releases its stack memory before that work and resumes afterward. OTA waits for the lease to finish. The scheduler allows an in-progress GATT exchange a bounded grace period and gives pending authenticated pairing up to 30 seconds. The app attempts bounded foreground reconnection, refreshes confirmed settings and does not replay mutations. Unconfirmed saves retain an error for the user to review.

## Final-image observation

The final image was reset out of the bootloader and observed over serial for 90 seconds with no app connected:

- Normal startup completed and saved Wi-Fi reconnected.
- Dexcom authenticated and **two glucose polling cycles returned readings**.
- BLE initialized and resumed after network operations.
- No crash/backtrace or TLS allocation error was observed.

| Final-image phase | Free heap | Minimum free heap so far | Largest free block |
| --- | ---: | ---: | ---: |
| Initial BLE initialization | 90,628 | 90,524 | 81,908 |
| First glucose worker start | 88,764 | 73,468 | 51,188 |
| First glucose worker end | 91,024 | 30,180 | 51,188 |
| Second glucose worker end | 90,124 | 24,132 | 51,188 |
| BLE resumed after second reading | 71,044 | 24,132 | 59,380 |

The glucose worker reported 5,108 and 5,476 unused stack bytes. A preceding corrective candidate's separate two-poll observation reached a lower **8,276-byte minimum heap**. Memory varies with other network work; the final 90-second result does not establish a safe long-term floor. Actual connected BLE sessions, weather/fleet combinations, manual web operations and OTA pressure still need qualification.

## Software verification and next test

The firmware build and slot-size check passed. All 45 repository Python tests and 15 Swift tests passed. The updated unsigned iPhone Release source build passed with the previously documented local asset-catalog exclusion. This does not constitute a signed app installation.

Use the latest app source from PR #29 so temporary network disconnects trigger its new foreground reconnect behavior. On the configured clock, hold **only the middle button for three seconds, then release** to open pairing; select the clock and enter its fresh displayed passkey when iOS requests it. Holding the middle button for ten seconds then releasing instead resets bonds, so release after the pairing gesture.

Authenticated iPhone Secure Connections, passkey visibility/timing, wrong-code rejection, bond recovery, saving and reading back brightness, alert continuity, long sessions, OTA/rollback and legacy migration remain pending in [BLE_ACCEPTANCE.md](BLE_ACCEPTANCE.md). No firmware release, installer promotion, TestFlight upload or App Store submission was performed.

## Button correction after owner feedback — 2026-09-06

The owner reported that the original outer-button pairing gesture powers off the TC001. Pairing now uses **only the middle button held for three seconds, then released**. Resetting bonds uses a ten-second middle hold followed by release. A 1–3 second middle hold still snoozes alerts, now on release, so pairing does not inadvertently suppress them. Short/double middle taps retain brightness/address actions. Pairing and recovery also work while the connection-address screen is shown. The `PAIR` indicator opens admission; the six-digit passkey is generated when the phone initiates authenticated pairing, not immediately on button release.

The corrected image is 1,585,712 bytes (249,296 bytes of slot headroom), SHA-256 `cc460201a6719255202e94517cf925374c1a22423a866fd22a7436c2d20aa34b`. All 46 Python tests pass, including a host test that executes the actual debounced button loop with controlled GPIO input: short/double taps, snooze versus pairing, bond reset, canceled combinations and suppression of unintended actions. The updated unsigned iPhone source build passes with the documented asset exclusion. App guidance and protocol/install instructions use the new gesture. Physical button timing and iPhone pairing still require the owner's hands-on check.

The corrective application-only flash was hash-verified and the clock was reset into normal operation. During the subsequent 90-second serial observation, saved Wi-Fi reconnected, Dexcom login succeeded, the first poll returned an empty provider array, and the second returned a reading. No crash or TLS allocation error was observed. The minimum free heap was 12,816 bytes; the final resumed BLE sample had 71,184 free bytes and a 55,284-byte largest free block. These are limited autonomous-operation measurements, with no active iPhone connection. Saved storage was not written or erased by this flash.
