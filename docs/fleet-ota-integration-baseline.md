# Fleet OTA integration baseline

The fleet branch is based on `3aec55b`, the merge commit for PR #21 (`d17cb6e`). The existing signed OTA layer is treated as a prerequisite and has not been rewritten in Phase 0/1.

## APIs provided by PR #21

- `ota_request_check()` downloads the compile-time `SUGARCLOCK_OTA_MANIFEST_URL`, validates the signed manifest, and records an available release.
- `ota_request_install(bool manual)` applies the already validated release to `esp_ota_get_next_update_partition(nullptr)` after the local safety policy passes.
- `ota_get_status()` reports state, progress, current/available versions, running/boot partitions, and boot-validation status.
- First boot remains pending for 15 seconds and calls `esp_ota_mark_app_valid_cancel_rollback()` only after local configuration, embedded assets, loop activity, and free heap pass. ESP-IDF rollback restores the previous slot otherwise.
- The signed manifest binds product, hardware, channel, version, minimum OTA version, size, SHA-256, immutable firmware URL, and key ID. Firmware bytes are downloaded over verified HTTPS and checked before the inactive slot becomes bootable.

## Known Phase 3 integration conflicts

These gaps are expected from the device-local PR and are recorded before changing their design:

1. The manifest URL currently defaults to GitHub `releases/latest`; a managed device will instead need the immutable manifest URL from an authenticated `ota_install` command.
2. Manifest channel validation uses compile-time `SUGARCLOCK_CHANNEL="stable"`; fleet operation needs a validated runtime `stable`/`preview` value in the dedicated fleet NVS namespace.
3. Automatic scheduling is one local hour plus daily jitter. Fleet maintenance windows require weekdays, start/end minutes, cross-midnight behavior, and an explicit audited override flag.
4. There is no public pause/pin API and no manual previous-partition rollback API. The latter must discover and validate the other A/B app slot locally rather than accepting a partition label from the server.
5. OTA retry backoff currently starts at 15 minutes and caps at six hours. Fleet check-in backoff is separate and must cap at 15 minutes without changing firmware update download retries.

Phase 1 therefore queues typed OTA commands but does not make them executable by firmware. Device-local safety checks and signed-manifest verification remain authoritative when Phase 3 connects the two layers.
