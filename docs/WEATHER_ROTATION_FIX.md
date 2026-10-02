# Weather rotation recovery (firmware 0.3.6)

The USB test clock had weather enabled with no API key, so every rotation
included the weather renderer's `WX...` waiting screen. Disabling weather on
October 1 persisted successfully and navigation excluded it. On October 2,
weather was enabled again without a key; the available diagnostics do not record
which client/action changed it. Automatic rotation remained enabled at 10 seconds.

Rotation now requires both the weather toggle and a received weather reading.
This applies to manual navigation and automatic cycling because they share the
same screen list. The existing five-second rebuild admits weather after its
first successful fetch, and removes a selected weather screen if data becomes
unavailable. A default-weather selection also falls back until data is available.
Weather fetching remains enabled according to user settings; the fix does not
delete credentials or switch off automatic rotation. Explicit diagnostic/server
forced screens retain their existing behavior. Existing cached weather freshness
behavior is unchanged.

Host regression coverage exercises enabled-but-empty weather, successful first
fetch, loss of weather data while selected, and disabled weather with cached
data. All 74 repository host tests passed. No iOS code or protocol changes are
required; TestFlight build 24 remains compatible.

## USB verification, October 2, 2026

Firmware binary: **1,593,728 bytes**, leaving **241,280 bytes** in the unchanged
application slot. SHA-256:
`e3af568f29deb213720a7473406b5128a7af6a1a73f3a88018b323a05af6c057`.
Firmware and installer filesystem builds passed; only the application was flashed.

A fresh private 4 MiB backup, USB MAC identity, partition-table match and active
OTA slot were verified before writing. Application flash hash verification passed.
USB communication failed during the separate metadata readback, including a ROM
read retry; byte-for-byte post-write metadata equality is **not verified**.
After a successful reset, Wi-Fi status confirmed firmware 0.3.6, valid provider
data and zero failures. Every returned configuration field matched the pre-update
snapshot, including weather enabled without a key, automatic rotation enabled
at 10 seconds, and the selected pet. Filesystem and the other OTA slot were
outside the application write range. No public firmware release was published.

A 36-second live status observation after reboot recorded TREND,
AMBIENT_CREATURE and GLUCOSE with no WEATHER state, while weather remained
enabled without a configured key. Automatic cycling is therefore verified on
the connected hardware through its status API; direct visual inspection of the
LED matrix was not available.
