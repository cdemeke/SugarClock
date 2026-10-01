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
