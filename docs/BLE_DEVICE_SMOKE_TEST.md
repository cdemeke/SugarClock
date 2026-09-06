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


## Pairing display correction after owner video — 2026-09-06

The owner's video showed glucose and `PAIR` overlapping. Source inspection found two LED submissions in the same main-loop iteration (normal engine, then BLE) plus a 600 ms pairing blink every four seconds. The middle-button hold did not fire its brightness action; another button assignment was unnecessary.

The main loop now composes normal, OTA and BLE layers before a single LED submission. The admission indicator is steady, a temporary network-radio pause shows `WAIT`, and successful authenticated pairing closes admission. Existing urgent glucose, buzzer/notification and OTA guards still take precedence. Normal engine evaluation and alert checking continue during pairing; no settings or button mapping changed.

Corrective image: **1,585,856 bytes**, SHA-256 `5b820ad1dbed7e7f9fa451afbe005b8f9e24e871fae17927c472b084ff4a0932`, with **249,152 bytes** of slot headroom. All **48 Python tests pass**, including the actual display implementation with a spy LED driver and the actual BLE render function with controlled status inputs. These verify one final submission, removal of underlying text, unchanged brightness, steady text across the previous blink boundary, temporary radio pause, passkey expiry, and yielding to urgent/OTA conditions. Firmware builds successfully. No iOS code changed for this correction.

The host tests do not establish physical LED appearance or iPhone pairing success. The owner must repeat the three-second middle hold-and-release and select the clock in the app to confirm a steady admission screen and readable passkey.

The application-only USB write was hash-verified, then the clock was reset into normal operation. During 90 seconds of observation, saved Wi-Fi reconnected and 2 polling cycles returned glucose readings, with no crash or TLS allocation error observed. Minimum free heap was 20,880 bytes; the final resumed BLE sample had 71,308 free bytes and a 59,380-byte largest free block. No active phone session or physical LED inspection was performed by the agent. NVS, filesystem and partition layout were not written or erased.

## Connection timeout correction — 2026-09-06

The owner reported repeated phone disconnections, gray/disabled screens and request timeouts. Source inspection found that the iOS app closed Bluetooth on any non-active scene phase, including temporary inactivity caused by a system pairing sheet. The firmware's prior 2.5-second forced network cutoff could also interrupt fragmented settings/schema transfers. A passive 65-second observation of the old firmware recorded three network radio pauses; it did not establish the phone-side disconnect reason.

The app now closes sessions only when backgrounded. Reconnection leaves navigation and drafts usable, disables only clock commands and exposes Stop/Reconnect controls. A complete paged schema is loaded atomically and retried after interruption; reading an already-connected saved clock keeps the current request-ID sequence. A delayed disconnect callback cannot tear down an already-connected replacement link. Unconfirmed mutations are still not replayed.

Firmware protects initial connection establishment (10 seconds), authentication and active request/response fragmentation, then waits for a 1.5-second idle gap. A 45-second total network-deferral ceiling preserves autonomous work even if a phone sends continuous traffic. A 1.5-second resume quiet period lets already-due network jobs share a radio pause. Short TLS/OTA radio interruptions remain; this is not a claim of continuous BLE/HTTPS coexistence. The clock now logs connection/authentication/disconnection state and reason codes without passkeys, credentials or payloads.

The candidate builds at 1,586,208 bytes with 248,800 bytes of slot headroom, SHA-256 `cbf48a9ffe69ef9fca581755190be24fb4c7748a104b80d4445a12902d8c878a`. All 48 Python tests and 18 Swift tests pass. Regressions cover a 342-fragment minimum-MTU transfer surviving the old cutoff, bounded autonomous network access, inactive/background handling, and interrupted schema loading. The updated unsigned iPhone source build uses the previously documented asset-catalog exclusion. No iPhone is connected to Xcode here, so the updated signed app has not been installed by the agent and real phone validation remains pending.

The application-only corrective flash was hash-verified and reset into normal operation. During the 90-second restart observation, saved Wi-Fi connected, both Dexcom authentication requests returned HTTP 200, and two glucose fetches returned empty provider arrays. No fresh reading, crash or TLS allocation error was observed. Minimum heap was 19,140 bytes; the second worker ended with 90,460 free bytes and a 51,188-byte largest free block. There were no phone connection/authentication events in this observation. Provider empty results must be distinguished from Bluetooth availability; do not report this run as successful data retrieval. NVS, filesystem and partition layout were not erased or written by the USB operation.
