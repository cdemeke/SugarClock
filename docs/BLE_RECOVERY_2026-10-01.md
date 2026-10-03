# Bluetooth and glucose display recovery — October 1, 2026

Candidate: firmware **0.3.4**, iOS **1.0.0 (21)**, BLE protocol **1**.
The ESP32 toolchain, NimBLE 2.5.0 dependency, partitions, settings format,
bond storage and signed Wi-Fi OTA path remain compatible with 0.3.2.

## Reproduced causes

- The USB test clock authenticated the phone, then crashed twice while pausing
  Bluetooth for HTTPS. Both captures reported `InstrFetchProhibited` with
  `0xfffffffd` and return address `0x400facf7`, symbolized against the matching
  0.3.2 ELF as `NimBLEDevice::host_task`. Restarts forced the app to repeat its
  connection and schema loading. The narrowly verified upstream timer shutdown
  [backport](NIMBLE_SHUTDOWN_BACKPORT.md) removes premature destruction of a
  queued callback and retains final cleanup after the host stops.
- The device was configured for a failing URL source with a 15-second polling
  interval, despite retaining configured Dexcom credentials. Restoring the
  saved Dexcom source and its 60-second fallback preserved all other returned
  configuration fields. Its existing timestamp-based schedule normally waits
  for the next approximately five-minute Dexcom reading and uses bounded
  failure backoff. No demo values or replacement credentials were installed.
- A successful real HTTP 200 response and fresh valid reading still left the
  display in `NET_LIMITED`. An old reachability failure could hide recovered
  readings for 15 minutes. New provider responses now update reachability;
  source/network changes discard old evidence. Fresh readings take precedence
  over an old failed probe. Authentication errors prove reachability only,
  never successful provider authentication or valid glucose. Wi-Fi event callbacks
  invalidate evidence even when an entire reconnect occurs while the main loop
  is occupied. Each HTTP attempt captures the Wi-Fi connection epoch before
  launch and publishes it atomically with the response code, so a late response
  from the previous connection cannot establish reachability for the new one.
  Failed transport results clear old success to unknown without adding an
  immediate extra TLS request or erasing a valid glucose reading.
- Discovery tried to pack a full name alongside a 128-bit service UUID into
  one legacy advertising payload and ignored the failure. Names now go into
  the scan response. The suffix uses the varying eFuse bytes rather than the
  shared vendor bytes; the authenticated full device identity is unchanged.
- Connection requests use a 6-second supervision timeout (30–60 ms interval,
  zero peripheral latency), following [Apple’s current guidance](https://developer.apple.com/forums/thread/822187).
  The phone controls the final parameters; sanitized connection/authentication,
  parameter-update and MTU diagnostics report the negotiated values. Requests
  occur once per connection, never in a renegotiation loop.
- Physical pairing/bond-reset gestures are now retained during a temporary
  network pause, when the Bluetooth stack is suspended.

## Fewer interruptions without delaying glucose indefinitely

Bluetooth still pauses for secure network operations because the previous
[continuous coexistence experiment](BLE_COEXISTENCE_TEST.md) exhausted memory
on this no-PSRAM hardware. This change does not enable that failed profile,
weaken pairing, suppress alerts or postpone glucose indefinitely. Existing
transfer/pairing protection has a 45-second ceiling, and auxiliary management
work can reuse an existing network pause.

Build 21 retains the build 19 optimization: it reuses a verified same-boot schema and timestamped status while
requiring a fresh settings read before enabling commands. A reboot, update or
capability change refreshes status/schema. The settings editor preserves
unsaved edits during recovery, labels old status, and never replays saves.
Add Clock cancels unrelated saved-device recovery, supports Stop connecting,
cancels when dismissed, and limits initial attempts to three. A new clock enters the saved library only after its settings, required status
and schema load successfully; an interrupted attempt cannot silently become
unlimited saved-clock recovery. Successful peripheral replacement preserves
the latest nickname even when renamed during loading. Existing saved clocks
retain foreground recovery with bounded retry delay. See
[iOS connection optimization](IOS_CONNECTION_OPTIMIZATION.md).

## Automated/build evidence

- 93 Swift tests passed; complete simulator build and signed Release archive
  passed. New coverage includes post-hello setup failures across retries and app
  recreation, peripheral replacement, concurrent renaming and persistent
  saved-clock recovery. Build **1.0.0 (21)** is confirmed **Testing** in the
  existing Internal TestFlight group on October 1, 2026; test notes are saved.
  Build 20 was uploaded during review and is superseded by build 21. Phone
  installation and UI acceptance are separate.
- 70 repository host tests passed, including BLE security/session ordering,
  persistence/migration, Wi-Fi, scheduling, OTA and TLS tests. Final reachability
  and glucose display regressions passed again after the last adjustment.
- The shutdown regression executes the actual pristine and patched upstream C
  functions: the original loses the queued callback; the patch passes 1,000
  simulated shutdown/reinitialization cycles. Patch integrity/version checks,
  repeat builds and interrupted writes are covered; CI reruns after resolving
  the pinned library.
- Firmware and installer filesystem builds and the layout check passed.
  Firmware size is **1,589,664 bytes**, leaving **245,344 bytes** in each
  1,835,008-byte OTA application slot: **1,520 bytes** above the tested 0.3.2
  baseline. SHA-256:
  `0cd3be36e119390f59534788ca618248f85712e810a46fcc723e54a8d5cbd75d`.

## Installation and physical verification

A fresh private 4 MiB backup was taken from the USB-connected clock at 115,200
baud. Its previous application matched the known 0.3.2 binary. The partition
table matched; valid OTA sequence 7 selected `ota_0` at `0x10000`. Installation
targeted only that application slot and verified its flash hash. The complete
first 64 KiB matched byte-for-byte before restart, including NVS/settings/bonds
and OTA metadata; filesystem and the second app slot were not written.
The unrelated Office clock is not part of
this test. Raw backups and configuration/reading data are excluded from this
repository.

After restart, saved Dexcom authentication succeeded, a real reading arrived,
and the web status confirmed `GLUCOSE`, valid data and zero provider failures.
Readback after reboot confirmed the restored source/interval and preservation
of every other field returned by the configuration endpoint. The first seven-minute capture recorded five real provider receipts, 12 phone
authentications, one boot and no captured crash. It also recorded eight
disconnects, including supervision timeouts after network pauses; this was
**not** a clean phone reliability pass. The subsequent 0.3.3 timing correction and negotiated-parameter diagnostics
were observed for six minutes: three real provider receipts, one boot, zero
captured faults and no phone connection. The lifetime minimum free heap was
43,592 bytes; the smallest sampled largest free block was 55,284 bytes. These
are observation-window measurements, not the smallest block inside HTTPS,
and do not establish the effect of the timing correction on an iPhone.
No public firmware release or installer artifact is promoted by this
development install.

The final **0.3.4** installation repeated the app-only flash, hash verification
and byte-identical 64 KiB metadata check using a fresh snapshot. A **180.2-second**
observation recorded one boot, one real Dexcom receipt, one captured network
pause and **zero captured faults**. The device reported firmware 0.3.4, HTTP
200, `GLUCOSE`, valid data and zero provider failures. The restored Dexcom source,
60-second fallback and configured password were confirmed after reboot.
Nine memory samples recorded a lifetime minimum heap of **42,108 bytes** and
a smallest sampled largest free block of **55,284 bytes**. No phone connected
during this final observation, so negotiated timing and app build 21 reliability
remain pending phone acceptance. USB observation was stopped; the clock was
left running.

## Remaining qualification

Repeat phone reconnect and confirmed saves across several natural provider
refreshes, app termination, background/foreground, radio toggles and range
loss. Verify two-clock selection, wrong/stale bonds and physical reset, urgent
alerts during Bluetooth traffic, OTA/rollback/reconnect, enterprise Wi-Fi and
legacy USB preservation. See [the acceptance checklist](BLE_ACCEPTANCE.md).
PR #29 still conflicts with newer mainline changes and requires integration
and revalidation before merge. A short USB observation is not long-term BLE
qualification or a guarantee of an uninterrupted radio connection.

## Follow-up diagnostic review (source only)

A provider failure arriving between DNS and data probes could clear the completed
DNS result without scheduling that stage again, leaving an incomplete summary.
The follow-up preserves completed direct-probe results during the active run
and resumes stages previously skipped using HTTP success. Completed TLS probes
are not repeated, and finished runs retain the usual retry schedule.

Three interleaving regression scenarios were added; the lost-DNS case fails
against the prior source. All **70 repository host tests** pass. The ordinary
ESP32 build passes at **1,589,776 bytes**, leaving **245,232 bytes** in each
unchanged 1,835,008-byte application slot. This follow-up is committed for review
and **has not been flashed**; the physical observations above refer to the
previous 1,589,664-byte installed 0.3.4 binary. App build 22 uses the existing
protocol and does not require this diagnostic change.
