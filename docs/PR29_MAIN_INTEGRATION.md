# PR #29 main-branch integration — 2026-10-02

Merged main `013da12` into the iOS/BLE branch without changing its partition layout,
toolchain, protocol version, app build number, or public installer artifacts.

The resolution preserves BLE session/reconnect and queued-settings behavior while
integrating Libre polling, production companion styles/colors, low-glucose display
locking, the redesigned web UI, fleet rollout authorization, and deferred OTA boot
validation. Web glucose connection tests retain the asynchronous worker contract.
Both companion regression suites are retained, including firmware/web pixel parity.

Configuration extensions are appended after the old BLE journal prefix. Recovery
accepts both previous journal sizes; new transactions use `pending_v2`, mirror the
legacy NVS keys, and remove the predecessor journal before removing the new one.
Libre and other configuration writers share a recursive mutex. Unrelated edits
preserve credentials and newly introduced fields; BLE responses redact Libre
credentials. See [migration constraints](BLE_MIGRATION.md#persistence-and-rollback-compatibility).

The fleet patch candidate now uses a checked heap allocation with credential
cleanup, keeping the fleet/OTA source stack frames within the existing 2,048-byte
limit. Low-glucose locking respects the app's glucose master switch.

## Verification

- Python host suite: 163 tests passed.
- Swift package: 149 tests passed.
- Site/demo Node suite: 51 tests passed.
- iOS simulator Debug build: succeeded, signing disabled.
- Standard ESP32 firmware and LittleFS builds: succeeded.
- BLE coexistence experiment build: succeeded (compile verification only).
- Partition, version, OTA boot-validation, and compiler stack-frame checks: passed.
- Largest checked frames: OTA manager 1,248; manifest 1,376; fleet manager 768 bytes.
- Standard firmware: 1,730,736 bytes of 1,835,008; 104,272 bytes remaining (5.7%).
- Fleet load test: 1,000 devices; backup/restore integrity passed and reconnect
  burst completed 1,000 requests with zero errors.

No hardware was flashed and no TestFlight/public release was uploaded for this
conflict-resolution task. This combined firmware still requires physical BLE,
Libre/network-memory, OTA/rollback, and migration qualification. Docker is not
installed in this environment, so the container build/health smoke test remains
for GitHub CI. Existing TestFlight build 24 is unchanged; the native app does not
yet expose main's new Libre-specific and companion-style controls (the web UI
does). Omitted fields remain preserved when existing app controls are edited.
