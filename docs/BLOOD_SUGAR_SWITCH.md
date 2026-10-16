# Blood Sugar switch

Firmware 0.3.1 adds `glucose_enabled`, defaulting to true for existing clocks.
The iOS Blood Sugar section exposes it beside the section heading; options hide
when it is off. Older firmware continues to show existing source settings with
an update explanation rather than a nonfunctional switch.

Saving Off disables glucose requests for Dexcom, custom URLs and demo data and
sets `alert_enabled=false` in the same configuration transaction. Manual refresh
cannot bypass it. Source credentials, thresholds and source selection are kept.
Turning readings back on resets the source session and fetch schedule so polling
resumes promptly; alerts stay off until explicitly re-enabled. Both normal and
Advanced alert editors prevent enabling alerts while saved readings are off.

A request already in progress may finish. Its result is discarded after a source
configuration change rather than published as a fresh reading. Dexcom login
stages check the setting before starting additional requests. The device does not
forcibly delete a running network task or remove TLS protection.

Glucose and trend leave the clock's rotation when readings are off. Glucose
buzzer alerts, urgent glucose overrides and stale/no-data warnings cannot replace
the remaining screens. If every screen is disabled, the time screen is the
fallback. Weather, management, notifications and firmware updates keep their
existing behavior; this switch does not promise uninterrupted Bluetooth.

The new NVS key is `glucose_en`. The field is appended to the ESP32 configuration
layout; the previous redo-journal prefix can still be recovered, defaulting the
new switch to On. Current journals preserve Off. Existing source and Wi-Fi keys
are not cleared. Downgrading to firmware before 0.3.1 does not honor the new switch
and can resume readings, so Off is not a downgrade-persistent feature.

## Validation

- 49 Swift tests, including the two-field Off patch and credential preservation.
- 57 repository tests, including actual HTTP scheduler behavior for all three
  sources, late completion discard, resumption, actual alert/display functions,
  firmware patch normalization and old/current journal recovery.
- Complete simulator and signed iOS Release archive builds pass. The Off screen
  was checked in the simulator.
- Normal firmware build passes at 1,586,800 bytes (248,208 bytes free in its slot).
  SHA-256: `d60ae893b79593b7f5de15b5adfbede8e2b0fc06be0580b1820a605f52f8a364`.

## USB verification, September 8, 2026

The normal image was written only to the active `ota_0` application slot and its
flash hash verified. Before restarting, the complete first 64 KiB matched a fresh
private backup byte for byte, preserving bootloader, partitions, OTA metadata,
NVS settings and bonds. Firmware 0.3.1 booted and received a Dexcom reading.

A physical test through the clock's local configuration API saved Off, confirmed
alerts were off, and compared all other exposed settings/credential-presence
flags with the initial snapshot. Attempting to enable alerts while Off kept them
off. Off survived a restart. Three manual fetch requests returned HTTP 409 while
Off, with no pending fetch, no new reading generation and no glucose/stale/no-data
screen over the observation window. Original readings/alerts preferences were
restored, and a fresh reading arrived after re-enabling. The first harness attempt
was interrupted by the expected HTTP connection timeout during reboot; preferences
were restored, and the corrected reboot-tolerant harness passed the complete test.

This checks the firmware path through HTTP, not the final phone/Bluetooth UI.
The owner's TestFlight Off/save/return/On test and physical Wi-Fi selection/join
remain useful follow-up checks.
