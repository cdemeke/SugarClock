# Installing and migrating to the iOS companion

Firmware 0.3.0 introduces protocol 1. The iOS app cannot discover or update older firmware over Bluetooth. It does not claim to identify an invisible old version. Keep the Mac installer and web portal available for initial installation and recovery.

## Local build artifacts

Build with `pio run` and `pio run -t buildfs`. The existing resource downloader at
`onboarding/TC001Setup/Scripts/download_tools.sh` can prepare the Mac installer tools
and copy a local firmware build into its source resources. For local verification
without modifying committed/published installer bytes, build the Mac app unsigned,
then stage the firmware into that **local** app bundle:

```sh
python scripts/package_local_firmware.py --output /tmp/sugarclock-usb-local \
  --app "/path/to/local/SugarClock Setup.app"
```

The staging tool records exact SHA-256 hashes, source version and protocol version.
It never flashes or publishes. The production preview/stable workflows attach the
protocol compatibility metadata and migration guide alongside the existing signed
firmware artifacts. Committed public installer files intentionally continue to match
their last published release until a separately authorized promotion.

## A. Stock or new clock

Build or obtain an explicitly approved release's USB artifacts. In the Mac installer, turn **off** “Upgrade existing SugarClock — preserve saved settings and certificates” for a stock/new device. Defer Wi-Fi configuration if using the iOS app. Install by USB, then launch SugarClock on the iPhone, allow Bluetooth, select the nearby clock, and enter its displayed passkey in the system prompt. If the initial two-minute pairing window expired, hold only the middle button for three seconds and release.

In the app, connect Wi-Fi using the clock's scan or enter a hidden SSID. Select Dexcom Share or a URL/Nightscout JSON endpoint, configure credentials, units and alerts, and confirm the separate network, persistence and provider/data results. Demo source is explicitly synthetic and is not a provider-success check.

## B. Configured clock with compatible signed OTA

Use the existing web updater or established managed signed-release path over its working Wi-Fi to install an approved 0.3.0-or-later release. Firmware-only OTA leaves the partition table, NVS and LittleFS untouched. After startup validation, open a physical pairing window and pair the iPhone. The app reads existing settings; it does not ask for passwords already configured on the clock.

The existing two `0x1C0000` slots and signed updater/rollback remain unchanged. The release must be signed by a key already trusted by that clock; installing an arbitrary unsigned URL is unsupported. This source change does not publish or promote a release.

## C. Older configured clock needing USB bootstrap

The old single-slot layout had LittleFS at `0x210000` (size `0x1F0000`). The current table puts it at `0x390000` (size `0x70000`), keeping NVS at `0x9000` (size `0x5000`). Merely avoiding `erase_flash` preserves NVS but **does not preserve filesystem certificates** if an empty/new filesystem image is written. The older migration command had that limitation.

Use the Mac installer's default **Upgrade existing SugarClock** option. It:

1. Reads a full 4 MiB recovery backup into `~/Documents/SugarClock Backups` with private file permissions.
2. Validates NVS and filesystem bounds in the clock's actual partition table.
3. Extracts LittleFS with the bundled `mklittlefs`, retaining certificates and unknown files.
4. Renames any unapplied `config.json` overlay to a `migration-config-…json` recovery file so stale installer values are not replayed over current NVS.
5. Rebuilds the preserved filesystem for the current 448 KiB partition and stops before flashing if extraction or packing fails.
6. Writes the existing bootstrap artifacts and preserved filesystem, without writing or erasing NVS. Installer form values and the post-install HTTP settings push are ignored in upgrade mode.

Backups and quarantined overlays may contain credentials; keep them private. They are recovery artifacts, not diagnostic attachments. If preserved files do not fit, export and review them before removing obsolete files; never discard a CA silently. Unrecognized stock filesystems are handled through the explicitly selected fresh-install path, not a guessed preservation migration.

The equivalent CLI procedure is below. These are instructions for an authorized physical upgrade, not commands run during this implementation. Replace the serial port and tool path with the actual environment:

```sh
python -m esptool --chip esp32 --port /dev/cu.YOUR_CLOCK --baud 115200 read_flash 0x0 0x400000 before-upgrade.bin
chmod 600 before-upgrade.bin
python scripts/prepare_ble_migration.py \
  --backup before-upgrade.bin \
  --mklittlefs "$HOME/.platformio/packages/tool-mklittlefs/mklittlefs" \
  --output migration-output
```

Inspect `migration-output/migration.json` and retain the backup. The preparation tool never flashes a device. After authorization to flash:

```sh
python -m esptool --chip esp32 --port /dev/cu.YOUR_CLOCK --baud 115200 write_flash \
  --flash_mode dio --flash_size 4MB \
  0x1000 .pio/build/esp32dev/bootloader.bin \
  0x8000 .pio/build/esp32dev/partitions.bin \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 .pio/build/esp32dev/firmware.bin \
  0x390000 migration-output/preserved-littlefs.bin
```

Do not run `erase_flash` or use the generic empty `littlefs.bin` for this preservation upgrade. A raw full-flash backup is the recovery route if bootstrap is interrupted. The USB bootstrap itself is not an atomic OTA operation; interruption while writing the partition table/bootloader can require USB recovery. Verify the new boot, original network, original glucose source and enterprise CA before subsequent OTA.

## D. Old firmware with broken Wi-Fi

Use the old firmware's `SugarClock-Setup` portal recovery, or the preservation USB procedure. The new app cannot discover, configure or transfer firmware to a clock whose installed firmware has no BLE service. The app's “Clock not found or pairing failed?” guidance explains this distinction.

## Persistence and rollback compatibility

Configuration continues to use the existing `tc001cfg` NVS keys and magic. A single `pending_v1` redo blob is written before mirroring known fields; it is removed only after successful writes/readback. Boot replays a pending blob before applying a normal installer overlay. Unknown NVS keys remain intact. BLE and web patch a copy of the current configuration, validate it, and serialize short mutation/persistence transactions with fleet, buttons and Wi-Fi commits. No network operation is performed while holding the BLE mailbox lock.

Normal rollback after a completed save reads the same legacy keys as before. **An older firmware does not understand `pending_v1`: rolling back or manually installing old firmware during an interrupted multi-key save can observe partial legacy mirrors.** Boot this Bluetooth-capable firmware to complete recovery before downgrading; retain the raw backup. Future firmware changing `AppConfig` layout must explicitly migrate any pending journal of the prior size/schema before accepting edits. This is a documented compatibility boundary, not a claim that old firmware has transactional recovery it never implemented.

BLE bond reset touches only the Bluetooth stack's bonds; Wi-Fi, glucose source, alert/display settings, certificates and fleet identity remain. Factory reset remains separate and records an intent to clear Bluetooth bonds on the next boot. The Improv serial path now queues the same Wi-Fi trial flow, validates packet lengths and preserves glucose credentials; intentional factory reset still clears application configuration.

After pairing, the app can request the existing signed Wi-Fi OTA check/install. Bluetooth disconnects temporarily to release heap, then reappears after check/failure/deferral or reboot. The app attempts bounded foreground reconnection, verifies full device identity, reads version and startup-validation state, and retains an expected version across app termination. A prior version plus `rollback_detected` is a failed update, not success.
