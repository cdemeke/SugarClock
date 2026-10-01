# Seven pixel pets in the Bluetooth companion firmware

The sprites, palette and named IDs are sourced from production commit
`013da12` on `main`: `include/companion.h` and `data/www/companions.js`.
This is a scoped backport, not a merge of the unrelated production firmware.

| ID | Name | Species |
| --- | --- | --- |
| 0 | Pip | Goldfish |
| 1 | Boo | Ghost |
| 2 | Mochi | Axolotl |
| 3 | Sprout | Dinosaur |
| 4 | Pebble | Turtle |
| 5 | Inky | Octopus |
| 6 | Maple | Red panda |

BLE schema, web settings and fleet configuration accept `ambient_character`
from 0 through 6. The schema advertises this canonical field. The iOS app prefers
it when supported; an older clock advertising only `ambient_creature` continues
to offer its supported fish/ghost choices. The app does not assume that installing
a new app adds new pets to old firmware.

The backport uses production's centered companion layout and retains the clock's
weather, seasonal, nighttime, interaction and urgent-reading safeguards. It does
not add production's separate text/icon layout or configurable status-color
settings. Existing NVS keys for those settings remain untouched. App artwork is
an awake sample, not a simulated live glucose reading. A shared fixture compares
77 animation/static frames against the actual C++ renderer and Swift artwork.

## Settings compatibility and migration

The existing `AppConfig` integer slot is reused without changing the binary
configuration or journal layout. NVS reads `pal_type` when present, falling back
to the older `amb_kind`. An existing ghost (`1`) therefore remains Boo (`1`).
Saving writes canonical `pal_type` plus the legacy supported `amb_kind` value:
Boo maps to `1`; all other pets map to `0`. Wi-Fi, glucose credentials, certificates
and unrelated configuration keys are unchanged.

The deprecated wire field `ambient_creature` remains a fish/ghost compatibility
alias: reads return `1` for Boo and `0` for every other pet. A legacy-only write
selects Pip or Boo. If both names are submitted, `ambient_character` takes
precedence regardless of JSON ordering. Modern clients should omit the legacy
alias from writes, because writing it back can replace one of the newer pets.

Rollback firmware can display only fish or ghost; the canonical `pal_type` key
is retained for upgrading again. **Changing the pet in rolled-back firmware does
not replace the retained canonical selection**: upgrading restores the last
seven-pet selection. This limitation affects the pet selection only. No cloud
service deployment or release publication is part of this change.

## Build and connected-clock verification, October 1, 2026

- App 1.0.0 (23): 143 Swift tests passed, signed Simulator build and signed
  iPhone archive passed. Uploaded successfully and confirmed Testing in the
  existing Internal TestFlight group; test instructions were saved.
- Firmware 0.3.5: 74 repository host tests passed. Production renderer tests
  exercise all seven pets, 28,350 frame combinations and 77 shared Swift/C++
  frames, with urgent/stale/missing-data safeguards.
- Normal firmware build: 1,593,712 bytes, with 241,296 bytes headroom in the
  unchanged 1,835,008-byte slot. SHA-256:
  `faa674c82a82b020f4b888f70325ed138a21ad66128512bb9a44489e93a3709f`.
  The installer filesystem artifact also built successfully.

The authorized USB clock install used a fresh private 4 MiB backup. A fast
backup attempt encountered corrupt serial data; the complete retry at 115,200
baud succeeded before any write. USB identity, partition layout, active slot
and valid OTA sequence were verified. Only the active application at `0x10000`
was written, and esptool verified its flash hash. The first 64 KiB were then
read back and matched the backup exactly before reboot, preserving bootloader,
partitions, NVS/settings/bonds and OTA metadata. The other application slot and
filesystem/certificates were outside the write range. No filesystem image was
flashed and no public firmware release was published.

After reboot the clock reported 0.3.5, Wi-Fi connected, GLUCOSE display state,
valid real-provider data and zero provider failures. Readback at 19 seconds
uptime reported a reading four seconds old. All 76 existing configuration
fields matched the pre-install snapshot; the only added response field was
`ambient_character`. Raw backups, configurations and glucose values remain
private and are not checked into the repository.

This short observation confirms startup, preservation and data recovery, not
physical iPhone reliability or the appearance of every pet on LEDs. Long-session
BLE, per-pet selection from the phone, alerts, OTA/rollback and VoiceOver still
need physical acceptance. PR #29 also requires mainline conflict integration
and revalidation before merge.
