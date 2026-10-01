# iOS connection optimization — September 30, 2026

This update is compatible with the existing protocol-1 BLE firmware. It changes the iOS connection coordinator, Core Bluetooth transport and read scheduling. It does not turn on the failed continuous-BLE/TLS firmware experiment or require a background Bluetooth entitlement.

## Behavior and rationale

- A known peripheral's native connect request remains pending for up to **60 seconds**, instead of being cancelled every 20 seconds. Discovery receives its own **15-second** budget after the radio connects. Core Bluetooth performs discovery for a pending connection itself, so the redundant explicit scan is stopped. [Apple documents that native connection requests have no implicit timeout](https://developer.apple.com/documentation/corebluetooth/cbcentralmanager/connect(_:options:)); our app still bounds individual attempts and cancels when backgrounded, stopped or switching clocks.
- Transient failures continue while the app is in the foreground. Delays increase **2, 4, 8, 16, 30 seconds**, capped at 30, instead of permanently giving up after five attempts. Only connection and read operations are retried. Permanent protocol/security errors stop, with specific stale-bond, pairing-timeout and full-storage guidance. Bluetooth-off waits for the existing powered-on event. My Clocks retains Stop connecting and permits switching clocks; loaded drafts remain usable.
- Empty response mailboxes are polled after **20, 40, 80, 160, then 200 milliseconds**. Previously each empty read imposed 200 ms. Valid response fragments remain immediate, with the existing acknowledged fragmentation/backpressure. This reduces unnecessary request latency while avoiding a busy loop if firmware is occupied.
- Completed schema metadata is cached locally, keyed by **verified device identity, boot ID, firmware, hardware, protocol version and capabilities**. Fresh authenticated hello, settings and status reads are still required before enabling commands. Clock reboot, update, capability change, missing identity or corrupt cache causes a reload. No configuration values, glucose readings, passwords, tokens or configured-secret flags are cached. Only the known schema metadata keys are eligible for persistence; unknown schema extensions remain in memory. Removing a clock removes its cache.
- Interrupted schema loads retain completed pages in memory, resuming from the first missing page only after the next authenticated hello proves the same boot identity. Partial schemas never enable editing or become persistent cache entries. Pages remain bounded to 16 fields, 11 pages and the existing 4,096-byte message size; duplicate keys and malformed completion are rejected.
- Cancellation of a pending GATT read/write drains the transport immediately. Cancellation callbacks and connection timers capture the session generation, so an old cancellation cannot close a newer connection.

The firmware's existing 60-second idle / ten-minute session limits and bounded BLE pauses for encrypted network work still apply. This is more persistent **foreground recovery**, not a promise of uninterrupted BLE or continuous background connectivity. OTA remains exclusively owned by its existing update monitor. Durable save acknowledgments/readback and the prohibition on automatic mutation replay are unchanged.

## Verification

- Baseline: 66 Swift tests passed.
- Candidate: **78 Swift tests passed**. New coverage includes recovery after ten transient failures, cancellation during backoff and explicit retry, permanent pairing errors, cache reuse after app recreation with fresh settings, corrupt cache, cache removal/privacy, boot/version/capability invalidation and interrupted schema continuation/restart. Existing save durability, secret handling, clock switching, background return and OTA ownership tests still pass.
- In the five-page schema test, reopening the app on the same boot sends **3 requests instead of 8**: hello, settings and status; all five schema requests are avoided. This is a measured request-count reduction, not a measured 62.5% reduction in radio connection time.
- **62 repository host tests passed**, including BLE/security, persistence, Wi-Fi, OTA and TLS checks after resolving pinned firmware libraries.
- Complete Debug simulator build including assets: passed (Xcode 27.0, build 27A266a).
- Signed iPhone Release archive **1.0.0 (17)**: passed. Xcode upload to App Store Connect succeeded; Apple accepted the package for processing. TestFlight availability/group assignment requires separate confirmation in App Store Connect.
- Existing companion firmware compiled with its unchanged pinned platform/libraries: **1,588,144 bytes**, **246,864 bytes** remaining in the 1,835,008-byte slot. No partition or firmware source changes were required for these app optimizations. Local PlatformIO runner was 6.2.0; the repository's CI runner remains pinned to 6.1.19.

Logs and archive are local under `/private/tmp/sugarclock-connection-*`. Raw device backups are private and excluded from the repository.

## Physical acceptance still required

Use TestFlight build 17 with BLE-capable firmware. Record time from selecting the clock to ready for both the first connection and a force-quit/reopen on the same clock boot. Repeat after rebooting the clock to exercise cache invalidation. Keep the app foreground for at least two glucose polls, leave/re-enter radio range beyond the previous five-attempt limit, toggle phone Bluetooth, and verify Stop connecting plus Retry. Save brightness twice and verify the clock and saved readback each time; repeat after background/foreground return. Include two clocks, wrong pairing code/stale bonds, interrupted saves, OTA reboot/rollback, and alerts during long sessions. Automated tests and compilation do not establish real iPhone RF reliability or end-to-end speed.
