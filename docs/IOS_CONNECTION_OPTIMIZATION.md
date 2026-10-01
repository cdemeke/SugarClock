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
- Candidate after review: **83 Swift tests passed**. New coverage includes recovery after ten transient failures, cancellation during backoff and explicit retry, permanent pairing errors, cache reuse after app recreation with fresh settings, corrupt cache, cache removal/privacy, boot/version/capability invalidation and interrupted schema continuation/restart. Existing save durability, secret handling, clock switching, background return and OTA ownership tests still pass.
- In the five-page schema test, reopening the app on the same boot sends **3 requests instead of 8**: hello, settings and status; all five schema requests are avoided. This is a measured request-count reduction, not a measured 62.5% reduction in radio connection time.
- **62 repository host tests passed**, including BLE/security, persistence, Wi-Fi, OTA and TLS checks after resolving pinned firmware libraries.
- Complete Debug simulator build including assets: passed (Xcode 27.0, build 27A266a).
- Signed iPhone Release archives **1.0.0 (17)** and **1.0.0 (18)**: passed. Both builds uploaded successfully to App Store Connect. Xcode confirmed build 18 was accepted for processing at 23:14 UTC. Build 18 includes the review corrections below. On October 1, 2026, App Store Connect showed **Missing Compliance** for build 18. After confirming that the app uses only Apple's operating-system encryption, the encryption question was completed and build 18 was added to the existing Internal group. Its status is now **Testing**, and the owner's specified Apple account is already the group's sole tester. Installation and radio testing on the owner's phone remain pending. The project's encryption declaration now records this implementation for future builds; this does not replace or re-upload build 18.
- Existing companion firmware compiled with its unchanged pinned platform/libraries: **1,588,144 bytes**, **246,864 bytes** remaining in the 1,835,008-byte slot. No partition or firmware source changes were required for these app optimizations. Local PlatformIO runner was 6.2.0; the repository's CI runner remains pinned to 6.1.19.

Logs and archive are local under `/private/tmp/sugarclock-connection-*`. Raw device backups are private and excluded from the repository.

The October 1 TestFlight follow-up also passed a complete Release simulator build. The built app's Info.plist was inspected with `plistlib` and its `ITSAppUsesNonExemptEncryption` value verified as Boolean `false`, not a string. The generator and checked-in project both include this declaration. No new phone build or firmware flash was needed to enable build 18.

## Pull request integration

The update is pushed to PR #29 and its Greptile review was requested. The companion branch already conflicts with newer mainline firmware, web, fleet and display changes; GitHub reports it as conflicting. Local tests above cover this branch, not a future merged tree. Resolve and reverify that integration before merging or promoting firmware. Unrelated edits in the original checkout were left untouched.

## Physical acceptance still required

Use TestFlight build 18 with BLE-capable firmware. Record time from selecting the clock to ready for both the first connection and a force-quit/reopen on the same clock boot. Repeat after rebooting the clock to exercise cache invalidation. Keep the app foreground for at least two glucose polls, leave/re-enter radio range beyond the previous five-attempt limit, toggle phone Bluetooth, and verify Stop connecting plus Retry. Save brightness twice and verify the clock and saved readback each time; repeat after background/foreground return. Include two clocks, wrong pairing code/stale bonds, interrupted saves, OTA reboot/rollback, and alerts during long sessions. Automated tests and compilation do not establish real iPhone RF reliability or end-to-end speed.

## Greptile review corrections (build 18)

A saved peripheral missing from Core Bluetooth's cache now gets a bounded 20-second rediscovery window. The scan remains active until the matching identifier appears, timeout, power loss or cancellation; it no longer starts a scan and immediately closes it. Discovery uses a production helper covered by discovery, cancellation and power-loss tests.

An interrupted save is immediately **unconfirmed** while foreground recovery continues. A later durable readback can resolve it, but no command is resent. Foreground retries remaining persistent is intentional for this request, with capped backoff, Stop connecting, clock switching and background cancellation. It does not prevent navigation or editing existing drafts.

New transport tests instantiate the production BluetoothTransport with its radio disabled and drive its actual read/write continuation and cancellation/completion boundaries. They cancel a pending operation, start a replacement session, deliver old-generation completions/cancellation, and verify that only the current completion finishes the new request. They also check operation backpressure. These tests do not emulate Core Bluetooth's OS delegate scheduling or establish RF reliability; that still needs the iPhone.

## USB device installation and smoke test

The attached clock had firmware 0.2.13 without the BLE service. A fresh private 4 MiB backup was completed before installation. The initial 460,800-baud read failed with corrupt serial data; the complete read, flash and verification at 115,200 baud succeeded. The partition table matched this build. OTA metadata selected valid ota_0 (sequence 7), so only the application at `0x10000` was written. The first 64 KiB captured immediately before flashing matched byte-for-byte afterward, covering bootloader, partitions, NVS and OTA metadata. The filesystem and second application slot were outside the write range.

Installed existing companion firmware **0.3.2**, binary SHA-256 `fe4ea04cfe9ad61e3303db2beb9314053085f2348b42e247cb0cacb6e296e529`. This is the PR's companion candidate, not a merge of newer mainline-only firmware features. A 90-second observation confirmed one boot, completed setup, six provider reading receipts and five BLE initializations, with no captured panic/allocation-failure signatures. Minimum reported heap was **42,036 bytes**; observed largest free block at HTTPS operation boundaries was **55,284 bytes**. These samples do not measure the smallest block during the request. No phone authentication occurred during the capture. Serial observation ended and the clock was left running.

Actual glucose values, credentials and passkeys were neither collected in the sanitized smoke summary nor committed. The complete flash backup remains private under `SugarClock Backups/connection-20260930`.


## Less disruptive recovery and Add Clock — build 19 (October 1, 2026)

Warm reconnection now sends **two readiness requests: hello and settings.get**, instead of three, when the verified device boot, firmware, hardware, protocol and capabilities match the previously loaded session. The app still reads the clock's actual settings before permitting a save and reuses only verified schema metadata. The last status snapshot is timestamped and refreshed by the foreground health check or an explicit refresh; it is not presented as a new reading. A clock reboot or changed firmware/capabilities refreshes status and schema. This supersedes the status-on-every-reconnect behavior above, without changing durable save verification or replaying mutations.

Add Clock drains any unrelated saved-clock recovery before discovery. Its pending connection is cancellable both with **Stop connecting** and by leaving the screen, and adding a new clock stops after three unsuccessful attempts with actionable retry guidance. Saved clocks retain persistent foreground recovery with capped backoff. Choosing Add Clock clears the active selection but preserves every saved clock and nickname; an existing clock remains available from My Clocks. The app prefers the advertised local name in discovery. Loaded editors remain usable through interruptions and explicitly retain unsaved edits until the owner can tap Save after reconnection.

Verification for build 19:

- **89 Swift tests passed**, including six new regressions for bounded new-clock attempts, cancellation, handing discovery control away from a saved clock, protecting writes/OTA from interruption, same-boot reconnect request counts and fresh status after a reboot.
- Complete Debug simulator build, including production SwiftUI views and assets: **passed**.
- Signed iPhone Release archive **1.0.0 (19)**: **succeeded**.
- App Store Connect confirmed build **19** as **Testing** in the existing **Internal** TestFlight group on **October 1, 2026**. This confirms tester availability, not installation or successful radio testing on the owner's phone.

The corresponding firmware **0.3.3** recovery work is recorded in [BLE_RECOVERY_2026-10-01.md](BLE_RECOVERY_2026-10-01.md). The TC001 still requires bounded Bluetooth pauses for encrypted network work; the app improvements do not promise an uninterrupted radio connection. Use build 19 for the physical acceptance steps above, including pairing the intended USB clock, repeated foreground glucose polls, two confirmed brightness saves, and background/foreground return. Physical iPhone UI, RF timing and long-session reliability remain unverified for this build.
