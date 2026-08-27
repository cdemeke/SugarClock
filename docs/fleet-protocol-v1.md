# SugarClock fleet protocol v1

This document freezes the Phase 0 wire contract between a clock and the local fleet service. The normative machine-readable contract and request/response examples are in [`fleet/protocol/v1`](../fleet/protocol/v1). The merged OTA API and known Phase 3 gaps are recorded in [`fleet-ota-integration-baseline.md`](fleet-ota-integration-baseline.md). Unknown object fields must be ignored so additive protocol changes remain forward compatible. Unknown command types must not be acknowledged as successful.

## Transport and identity

Production devices use certificate-verified HTTPS. On first managed boot the clock creates a canonical random UUID installation ID and a 32-byte random credential, both in the dedicated `sugarfleet` NVS namespace. It sends the credential as `Authorization: Bearer <base64url credential>` on registration and every later request. The server stores only a salted, iterated PBKDF2-SHA256 representation of the credential.

Automatic registration classifies an installation as `unverified`; it does not prove official hardware ownership. A reused installation ID with a different credential is rejected. Registration never returns commands.

## Registration

`POST /device/v1/register` accepts `application/json` with the fields shown in `register-request.json`. A successful response is `201` for a new installation or `200` for an idempotent retry. `next_checkin_seconds` is a target; firmware adds jitter so steady-state check-ins occur every 90–150 seconds.

## Check-in

`POST /device/v1/check-in` overwrites the device's current operational snapshot and returns up to 16 pending commands. Optional `device_nickname` and `device_location` strings report the labels saved on the clock; they do not overwrite administrator-owned labels. It must never include glucose readings, CGM or Wi-Fi credentials, SSID, MAC address, raw logs, source URLs, or raw failure text. Health and result values are sanitized identifiers.

The service can redeliver an unacknowledged command. Devices therefore retain at least 32 recently completed command UUIDs in NVS and return the prior result without reapplying a replayed command. Core clock operation does not depend on check-in success. Firmware retries failures with exponential backoff capped at 15 minutes.

## Command results

`POST /device/v1/commands/{command_id}/result` accepts `accepted`, `deferred`, `succeeded`, or `failed`. Final results are idempotent when the repeated status and sanitized result match; a conflicting final result returns `409`.

The v1 command types are `config_patch`, `set_channel`, `set_maintenance_window`, `ota_check`, `ota_install`, `ota_pause`, `ota_rollback_previous`, `restart`, and `notify`. Commands carry UUIDs and creation/expiration Unix timestamps. An `override_window` never overrides the firmware's local safety checks.

## Errors

Errors use an HTTP status plus `{"error":{"code":"sanitized_identifier","message":"safe text"}}`. The service does not include credentials, raw authorization headers, secret configuration values, request bodies, or source IPs in an error.

## Phase boundary

The server deliberately rejects secret and connectivity-related `config_patch` fields, including SSID, enterprise identity, source URL, and credentials. Those fields remain disabled until a later phase adds encryption at rest, write-only UI behavior, device-side transactional Wi-Fi application, and recovery tests. The firmware integration below extends the entry point to the existing OTA executor without weakening its trust or safety policy.

## Firmware integration status

Firmware v0.2.3 implements persistent device identity, registration, periodic check-in, command results, maintenance windows, production/fallback endpoint selection, and managed signed OTA installation. Managed OTA commands reuse the existing A/B executor and cannot bypass local battery, glucose, alarm/timer, setup-mode, trusted-manifest, hash, or boot-validation safeguards. Network clients are serialized so a fleet TLS handshake cannot overlap glucose or weather TLS work on the ESP32.
