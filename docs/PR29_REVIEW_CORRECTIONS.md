# PR 29 review corrections — firmware 0.3.2 / iOS build 11

## Behavior

- Blood Sugar Off adds the alert-disable dependency only to the submitted patch. Off then On before saving no longer leaves a hidden alert edit. Explicit alert edits remain intact. Once Off is saved and confirmed, enabling readings leaves alerts off; delayed confirmation preserves later reading-switch edits without restoring alerts.
- Bluetooth, web and fleet settings patches share `settings_apply`. After persistence, screen rotation, manual brightness and time-zone effects use the configuration actually retained by `config_save`. A rejected journal can restore committed settings; a pending recovery journal retains the candidate. Both paths reconcile runtime effects while still reporting persistence failure. Failed transactions also invalidate provider work conservatively, covering rollback. Successful cosmetic changes do not provoke a provider refresh.
- Wi-Fi scan-start failure and later driver failure are reported over Bluetooth instead of presenting a stale cache as a successful scan. An already-running scan can be reused. Starting a successful retry clears failure state. Wi-Fi trials mark interrupted scans failed. The web page labels previous results when the latest search failed; the app retains its previous list and reports search failure without unnecessarily reconnecting Bluetooth.
- The firmware-update monitor owns reconnection until it completes or is cancelled. Automatic reconnection, explicit reconnect/retry and switching to another clock cannot take over during monitoring. The power-state publisher is injectable for regression tests. The pre-existing busy flag already serialized requests; this adds ownership across the monitor's waiting intervals.
- Reconnecting updates saved clocks in place; new clocks append. The old `clock.selected` preference is removed when preferences are saved. Launch still starts on My Clocks and only preconnects when exactly one clock is saved.

## Automated validation

- 58 Swift tests pass. New cases cover the unsaved toggle reversal, explicit alert edits, delayed confirmation of a saved Off, scan failures at start/completion, stable clock ordering and persisted order, retiring the selection preference, and radio/foreground/explicit reconnect events during update monitoring.
- 59 repository Python tests pass. New C++ harnesses execute production settings-application and Wi-Fi/Bluetooth scan code against controlled persistence and driver outcomes. Persistence cases cover success, journal rejection, mirror failure and journal-cleanup failure.
- Complete iOS simulator build and signed Release archive 1.0.0 (11) pass.
- Normal `esp32dev` firmware builds at 1,587,200 bytes, with 247,808 bytes free in the application slot. Static RAM remains 93,544 bytes. SHA-256: `97cfd3be5674174745491e39062703fddc26ac4349f42d64a9369431e9997073`.
- Both web page copies and the generated embedded web asset are synchronized.

## Physical qualification

On September 8, 2026, a fresh full-flash backup and partition/OTA-metadata check identified the active application at 0x10000. Only that application was replaced. Flash verification matched the new image, and the entire first 64 KiB (bootloader, partitions, settings/bonds and OTA metadata) matched the backup byte for byte before restart. The clock booted as 0.3.2 and received a glucose reading. A local-API setting change saved and read back correctly; all original exposed settings were restored and verified. An explicit Wi-Fi search completed with 12 networks, and a glucose reading remained available. Persistence and Wi-Fi driver failures are injected in host tests, not induced on the owner's live clock. End-to-end iPhone alert-toggle, Bluetooth power-toggle during OTA, and multi-clock behavior remain physical follow-ups.
