# Companion implementation verification

Recorded 2026-09-05 for source version **0.3.0**, protocol **1**. This is an implemented development candidate, not a hardware-qualified release. No physical clock was flashed, firmware distributed, release published, or TestFlight/App Store upload performed as part of this implementation.

## Implemented slice and dependency decision

The first firmware/app slice implemented discovery, system passkey pairing, authenticated device information, brightness patching, persistence acknowledgment and readback before expanding the settings interface. The real transport uses Core Bluetooth and authenticated NimBLE GATT access; mocks are explicit debug previews/tests only. End-to-end radio execution remains pending.

Pinned **NimBLE-Arduino 2.5.0** from the maintained 2.x line after evaluating its upstream security/server APIs and compiling it against the existing **espressif32 7.0.1 / Arduino 2.0.17** toolchain. No Arduino/ESP-IDF migration was needed. The application uses its peripheral/server role; unused central/observer roles are disabled, with one connection and four bonds. Upstream references: [tagged source](https://github.com/h2zero/NimBLE-Arduino/tree/2.5.0), [releases](https://github.com/h2zero/NimBLE-Arduino/releases), [API documentation](https://h2zero.github.io/NimBLE-Arduino/). Compatibility here means successful source compilation; actual iPhone LE Secure Connections negotiation must still be tested.

BLE startup checks returned initialization failures and leaves normal clock startup available. Glucose HTTPS runs on a bounded worker so display/alert processing can continue. BLE is suspended for the existing Wi-Fi OTA worker and restarted after checks, failures or deferrals. Short configuration transactions serialize mutation without holding a lock during network operations. These mechanisms require the timing and memory checks below on real hardware.

## Firmware measurements

Baseline: clean commit `17cbb2f1abf1dc2e331a2b6ce70d2db3b70a4339`, version 0.2.7. Both builds use the original partition table and pinned ESP32 toolchain. ESP32-WROOM TC001 is configured for 4 MiB flash without PSRAM.

| Measurement | Baseline | Companion firmware |
| --- | ---: | ---: |
| Firmware binary | 1,318,656 bytes | 1,585,280 bytes |
| ELF flash usage | 1,312,073 bytes | 1,578,697 bytes |
| Static RAM | 73,256 bytes | 93,504 bytes |
| Each OTA slot | 1,835,008 bytes | 1,835,008 bytes |
| Binary headroom per slot | 516,352 bytes | 249,728 bytes |

Final binary occupies **86.4%** of a slot, so the existing greater-than-85% utilization warning fires; the size check passes. Signed OTA and both rollback slots remain. Binary SHA-256:

```text
c21093a2bd6d8798514928616ec59e0c948af60069413bde24ea4e020bce1f6f
```

Latest machine-readable results (including subsequent fixes): [BLE_BUILD_METRICS.json](BLE_BUILD_METRICS.json). Static RAM is not free runtime heap. **Runtime minimum heap, largest free block, stack margins and BLE/HTTPS/OTA coexistence have not been measured on hardware.** Firmware emits phase-specific BLE, glucose and OTA memory samples; authenticated status also reports free/minimum heap and largest free block. Use [BLE_ACCEPTANCE.md](BLE_ACCEPTANCE.md) to record actual measurements and repeated-session results.

## Completed local checks

| Check | Result |
| --- | --- |
| `pio run` | PASS, firmware and original slot-size checks |
| `pio run -t buildfs` | PASS, 458,752-byte LittleFS image |
| `python -m unittest discover -s tests -v` | PASS, 45 tests, no skips in final run |
| `swift test --package-path ios` | PASS, 10 XCTest cases, zero failures |
| `python scripts/check_layout.py` | PASS, source 0.3.0; published USB artifacts remain 0.2.2 |
| Unsigned Mac installer Xcode build | PASS, including preservation migration and process-path quoting |
| Unsigned iPhone Release SDK build, asset catalog explicitly excluded | PASS, actual app source linked; privacy manifest bundled |
| Normal final iOS asset-catalog packaging | BLOCKED by local Xcode/CoreSimulator mismatch |
| `git diff --check` | PASS |

Repository tests cover bounded protocol framing and shared fixtures, malformed/duplicate input, security admission predicates, patch validation/omitted fields/redaction, fault injection through the actual journal transaction function, Wi-Fi recovery policy, real LittleFS migration extraction/repacking, existing signed OTA tooling/policy, and fleet behavior. Swift tests cover framing/compatibility, connection lifecycle, timeouts, queued versus saved responses, confirmed brightness readback, no automatic mutation replay after disconnect, and secret actions. Security predicate tests do not execute the platform pairing stacks; Wi-Fi policy tests do not perform radio association. No XCTest case is represented as a physical BLE test.

Build tools used: PlatformIO environment with `framework-arduinoespressif32 3.20017.241212+sha.dcc1105b`, Xtensa GCC `8.4.0+2021r2-patch5`, esptool 4.11.0; Xcode 26.6 (`17F113`) with iOS SDK 26.5. Tests used the repository's Python dependencies (`requirements-build.txt`, `fleet/requirements.txt`) and the bundled Xcode Swift compiler.

## Local runnable source and build outputs

Open [the iOS project](../ios/SugarClock.xcodeproj) with [build/run and TestFlight preparation instructions](../ios/README.md). The checked-in project includes normal branding assets, Bluetooth usage text, privacy metadata and no signing identity. Mac installer source remains under `onboarding/TC001Setup`.

Local outputs produced during this run (temporary paths, not distribution artifacts):

- `/private/tmp/sugarclock-ios-development/SugarClock.app`: unsigned iPhone Release build without asset catalog; requires owner signing for a phone.
- `/private/tmp/sugarclock-mac-build/Build/Products/Debug/SugarClock Setup.app`: unsigned Mac installer, locally staged with the new firmware.
- `/private/tmp/sugarclock-usb-0.3.0`: firmware, bootloader, partition table, boot metadata, LittleFS, protocol compatibility and SHA-256 manifest. Never use its generic filesystem for a preservation upgrade; follow the migration guide.
- `.pio/build/esp32dev/firmware.bin`: exact measured firmware image.

The development iOS build command was:

```sh
xcodebuild -project ios/SugarClock.xcodeproj -target SugarClock -sdk iphoneos \
  -configuration Release CONFIGURATION_BUILD_DIR=/private/tmp/sugarclock-ios-development \
  CODE_SIGNING_ALLOWED=NO EXCLUDED_SOURCE_FILE_NAMES=Assets.xcassets build
```

The full normal build was attempted. This host has CoreSimulator 1051.50 while Xcode expects 1051.55; the installed simulator runtime build 23E254a does not match SDK build 23F81a. Asset compilation reports no matching simulator runtime, and scheme destination resolution also fails. Administrator installation/repair of the matching Xcode components is required. An unsigned source build with explicitly excluded assets is not a successful full distribution build, simulator launch, or signed device install. Repair the environment and run the normal commands in the iOS README before archiving. CI includes a normal iOS build, but that remote job has not been run here.

## Remaining checks and explicit limitations

All [physical acceptance checks](BLE_ACCEPTANCE.md) remain pending, particularly authenticated Secure Connections on ESP32/iPhone, wrong-passkey rejection, bond recovery, alerts during HTTPS/BLE pressure, repeated reconnection, power-loss recovery, configured OTA upgrades, legacy USB migration, OTA rollback and measured memory margins. No connected device was used to establish these claims. VoiceOver, Dynamic Type and dark-mode behavior use native controls but still require hands-on review.

New enterprise CA upload remains in the existing web interface. BLE advertises preservation/use of existing enterprise certificates and the app states that boundary. It does not silently weaken certificate validation. During OTA the app displays the planned disconnect and reconnect state; it cannot stream live progress while BLE is suspended, so the clock displays progress and the app confirms resulting version/validation after reconnecting.

Completed settings saves preserve prior-firmware NVS keys. An older firmware cannot replay the new journal after an interrupted multi-key save; complete recovery on 0.3.0 before downgrading. USB bootstrap itself is not atomic. See [migration instructions](BLE_MIGRATION.md) for all four installation paths, backups, certificate/overlay handling and recovery constraints.

An Apple development/distribution team, owner-controlled app identifier, working Xcode runtime components and a signed iPhone install/archive are still required. No team ID, signing credentials, release signature, upload or physical flash is invented or implied by these results.

## Screenshot follow-up

The installed CoreSimulator binary was subsequently invoked directly, bypassing the Xcode wrapper's attempted component repair. The existing **iOS 26.4 / iPhone 17 Pro simulator** successfully ran an unsigned Debug simulator build. Eight [screenshots](screenshots/README.md) capture the actual SwiftUI views with explicit sample state and a visible preview label; Core Bluetooth and settings interaction are disabled in this Debug-only mode. The original logo PNGs were copied into the local simulator bundle because normal asset-catalog compilation remains blocked. This establishes simulator rendering, not physical BLE reliability or a full distribution build. The 10 Swift tests and iPhone Release source build were rerun after adding screenshot support.

## Native design follow-up — 2026-09-06

The iOS interface now follows PR #30’s palette, cards and original icon assets. The gallery contains 13 simulator captures, including light/dark configuration pages and an accessibility-size text layout. Grouped editors use a tested draft model that preserves omitted values, secret actions and exact thresholds during unit-only changes. All 15 Swift tests pass; simulator Debug and unsigned iPhone Release source builds pass with the same documented local asset exclusion. The prior PR commit’s GitHub firmware, fleet and normal iOS CI builds passed; the design commit triggers another CI run. No firmware source or partition changes were made in this design follow-up. Hardware and signing acceptance remains pending.

## Physical-device follow-up — 2026-09-06

The owner subsequently authorized flashing the connected configured TC001. The application-only USB upgrade preserved the existing partition table, filesystem and all 79 application-configuration entries. Actual hardware exposed a Dexcom TLS allocation failure with BLE resident; the corrected build releases BLE memory during scheduled HTTPS work and resumes advertising afterward. The app now attempts bounded foreground reconnection without replaying mutations. See [the device smoke-test report](BLE_DEVICE_SMOKE_TEST.md) for exact final-image measurements, observations and remaining checks. These results supersede the earlier statement that no physical clock had been flashed; authenticated iPhone pairing and full acceptance remain pending.

The corrected firmware builds successfully at 1,585,760 bytes with 249,248 bytes of slot headroom. All 45 Python tests and 15 Swift tests pass. The updated iPhone Release source build passes with the previously documented asset-catalog exclusion; no new signed phone install or distribution build is claimed.
