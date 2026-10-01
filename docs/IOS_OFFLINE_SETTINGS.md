# Offline settings and explicit clock updates

The companion's editors share one local draft per saved clock. Connect once to
load its supported fields and saved settings; afterward, edit across screens or
reopen the app while the clock is unavailable. The app labels the last read time
and separates pending edits from confirmed settings.

## User flow

1. Change display, glucose source, alerts, time or other supported settings.
   Navigation retains the draft. Viewing settings does not change values.
2. Open **Review changes** to compare each proposal with the last clock value.
   Secret replacements are never displayed there. **Use clock value** removes
   just that proposal, preserving other edits. Discarding all changes requires
   an explicit confirmation.
3. Tap **Update clock** once to submit the current batch. It can be tapped while
   disconnected. The app waits up to 90 seconds in the foreground, reconnects,
   reads the current settings and validates the batch before sending.
4. **Cancel update** during waiting keeps the draft. Backgrounding or switching
   clocks cancels unsent work. Relaunching never automatically sends a draft.
5. After sending, the app requires durable acknowledgment/readback before it
   reports success. Edits made after tapping Update remain pending for a later
   update. An uncertain result stays unconfirmed; no mutation is replayed.

Wi-Fi replacement retains its separate **Test connection, then save** workflow.
It uses the clock's trial connection and only replaces the previous network
after success. Firmware updates, scans and commands are not settings drafts.

## Storage and privacy

The workspace contains a redacted settings snapshot, schema, original edit
baseline, draft and last-read timestamp. It does not contain glucose readings
or diagnostic history. The entire record is stored in Apple's Keychain using
[`kSecAttrAccessibleWhenUnlockedThisDeviceOnly`](https://developer.apple.com/documentation/security/ksecattraccessiblewhenunlockedthisdeviceonly)
and [`kSecAttrSynchronizable = false`](https://developer.apple.com/documentation/security/ksecattrsynchronizable).
There is no plaintext preferences fallback and no required account or server.

Existing clock credentials remain configured indicators. Only explicit pending
replacements contain new secret values. Confirming or discarding those edits
removes their values from the draft; returning a secret to Leave unchanged or
Clear also removes replacement text. Removing a clock deletes its workspace.
Keychain records may survive app reinstallation, but a record never authorizes
an automatic update. Device migration does not copy these device-only records.

Records are versioned and bounded. A failed read must not be overwritten by a
new empty workspace, and a failed save must not silently discard in-memory
edits when choosing another clock. Preview/test stores are explicitly injected;
a real storage or radio failure never selects a mock backend.

## Update correctness

The current batch is tied to the verified clock identity and peripheral.
Only read-only connection/preflight work retries before the deadline. Normal
commands and OTA cannot overlap the update. The batch is sent as one existing
protocol-v1 `settings.patch`, with a complete UTF-8 envelope size check against
the firmware limit; oversized batches are retained and rejected, never split.

Fresh schema validation rejects removed or retyped edited fields, new bounds,
invalid threshold combinations and unsupported values. Settings not edited by
the user are omitted. Threshold unit presentation retains exact mg/dL values.
Firmware normalization of disabled services is accounted for in confirmation.

A change to an edited field since its local baseline requires review instead
of being silently overwritten. Choose Use clock value for that field, then
edit it again if needed. This comparison is best effort: protocol v1 has no
atomic revision/compare-and-swap operation, and secret configured flags cannot
reveal whether an already-configured secret changed elsewhere.

The persisted submission marker records an uncertain attempt; it is not a
queue that drains after restart. A disconnect, timeout or persistence failure
after a mutation may mean it was applied. The app retains edits and labels the
result unconfirmed. A subsequent explicit update is a new user action.

## Build 22 verification

- Complete signed Simulator build and signed iPhone Release archive **1.0.0 (22)** passed.
- Build 22 uploaded successfully and was confirmed **Testing** in the existing
  Internal TestFlight group on October 1, 2026, with test instructions saved.
  Availability does not establish installation or physical phone acceptance.
- **126 Swift tests passed**, including offline restoration, multi-screen draft
  state, concurrent later edits, read-only preflight retries, bounded waiting,
  cancellation during preflight, background/switch/removal, missing fields,
  conflicts, oversized messages and uncertain secret updates without replay.
- Secure-store failures are tested for read, save, discard and deletion;
  a failed pre-send marker write cannot create a false attempted-update marker.
- The real Keychain backend passed a signed iOS Simulator smoke check: save,
  reopen through a new store instance, accessible/synchronizable attributes,
  replacement-secret cleanup and removal. This check uses a random isolated
  test identity and deletes its record. It is compiled only in Debug.
- Simulator screenshots use explicit sample fixtures in the production views;
  they never substitute for a failed real connection. Offline editing, waiting
  and Dynamic Type layouts are available through `SUGARCLOCK_SCREENSHOT`.

For the Keychain smoke check, build the simulator target with your Xcode team
and signing enabled, then launch with `SIMCTL_CHILD_SUGARCLOCK_SCREENSHOT=keychain-smoke`
using `simctl launch`. A build with signing disabled did not provide the
simulator identity required by Keychain; the signed build passed. This does
not establish locked-device behavior on a physical iPhone.

## Physical acceptance

- Load a clock, disconnect it, edit several categories and reopen the app.
- Tap Update while offline, reconnect within 90 seconds and confirm one batch.
- Edit again while waiting/sending; confirm later edits stay pending.
- Cancel, background, switch clocks and restart while waiting; verify no send.
- Change the same field through the web interface; review the conflict.
- Test a lost acknowledgment, secret replacement and interrupted persistence.
- Verify Keychain access while unlocked, device lock/relaunch and clock removal.
- Repeat with two clocks and across the firmware's natural HTTPS pauses.

Simulator checks and automated state-machine tests cannot establish physical
Bluetooth reliability. This is an app-only change using existing firmware
0.3.4/protocol v1; no new USB flash is required.
