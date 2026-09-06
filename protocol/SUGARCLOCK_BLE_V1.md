# SugarClock BLE management protocol 1

Status: implemented in firmware 0.3.0 and the iOS companion; physical ESP32/iPhone acceptance remains required. Android implementations can use the same GATT attributes and JSON messages. No account, backend, or app-owned pairing secret is required. The existing glucose engine, alerts, web portal, and signed OTA updater remain clock-owned.

## Discovery and identity

| Attribute | UUID | Properties |
|---|---|---|
| Management service | `ca7c0001-63a2-4b7c-9a5b-763e4e0c1000` | Advertised 128-bit service |
| Request | `ca7c0002-63a2-4b7c-9a5b-763e4e0c1000` | Write with response; authenticated encryption required |
| Response mailbox | `ca7c0003-63a2-4b7c-9a5b-763e4e0c1000` | Read; authenticated encryption required |

Advertisements/scan responses contain `SugarClock-XXXXXX`, using the last six characters of the eFuse-derived identity, and the service UUID. No glucose values, SSIDs, passwords, tokens, or configured-source indicators are advertised. `hello.device_id` is the full uppercase 12-character eFuse MAC representation; it survives ordinary firmware updates and configuration/bond resets. It is an identifier, not an authentication credential. Do not trust advertisement names for authorization. iOS stores its Core Bluetooth peripheral UUID alongside the authenticated full identity. A replacement mainboard has a different identity. A suffix collision is possible; confirm the displayed passkey on the selected physical clock.

`hello` returns `v`, `id`, `state`, `device_id`, `name`, `boot_id`, `firmware`, `hardware`, `max_message`, `max_packet`, `ota_disconnect`, and `capabilities`. `boot_id` is an opaque boot nonce. Current firmware retains it across temporary Bluetooth suspension; early development builds regenerated it on BLE restart. Always scope request IDs to a connection, regardless of nonce equality. Hardware is `ulanzi-tc001-esp32-4mb`.

Current capabilities: `settings.patch`, `wifi.trial`, `wifi.enterprise.preserve`, `ota.signed_wifi`, `bonds.physical_reset`, `schema`, `network.pause`. Additive response fields/capabilities must be ignored by older clients. An unknown request operation or patch field is rejected; it never resets unrelated settings. Negotiate protocol major 1 and limits before editing. A new incompatible major needs an explicit client migration; never infer compatibility from firmware version alone.

## Authentication and physical admission

Use standard bonded LE Secure Connections, authenticated with a fresh six-digit passkey displayed on the TC001. Firmware sets display-only I/O, bonding, MITM, SC-only and 128-bit encryption; it distributes identity keys for reconnecting private-address phones. No Just Works fallback, universal PIN, custom encryption, or app PIN database exists. iOS owns the pairing prompt and bond keys.

New unconfigured clocks with no bonds open a 120-second boot admission window. Configured clocks require pressing **left and right together for at least 3 seconds, then releasing both**. The chord consumes individual gestures. Hold both for **10 seconds** to remove all bonds and reopen admission, preserving NVS application settings and LittleFS certificates. This reset is physical-only: `bonds.reset` returns `physical_action_required`. `status.get` reports bond count and admission-window state. Four bonds maximum; a new phone is refused when full. The custom store handler does not evict another phone's bond.

Known bonds can reconnect outside admission windows. Unknown peers are disconnected before management. Every management callback checks encryption, authentication, bonding, key size, connection handle, and SC-only policy. There are no private notifications or indications in v1: all private content is obtained by authenticated reads. ATT permissions also require encryption/authentication before callbacks.

A wrong code or unauthenticated link does not grant access. NimBLE's repeat-pairing behavior may remove a stale bond; the passkey callback still refuses new pairing outside the physical window. For recovery, reset bonds physically and also choose Forget This Device in iOS Bluetooth Settings. A factory reset is a separate existing operation. Lost phones' bonds should be removed through the physical reset.

The passkey is rendered as six compact digits for up to 30 seconds. Active buzzer/urgent glucose/urgent notifications take display priority. Retry pairing if an urgent event prevented reading the code. Unauthenticated connections expire at 45 seconds. Authenticated connections expire after 60 seconds without a management read/write or after 10 minutes total. Apps disconnect when leaving foreground. No background BLE mode is required.

## Framing and mailbox flow

All integers in the eight-byte header are unsigned little-endian. A GATT value is at most `min(180, negotiated ATT MTU - 3)` bytes. The client uses its platform's maximum write-with-response length, capped at 180. Default MTU 23 works with twelve payload bytes per frame.

| Byte | Meaning |
|---|---|
| 0 | Protocol major, `1` |
| 1 | Kind: `0` request fragment, `1` response fragment, `2` response-offset acknowledgment |
| 2–3 | Nonzero request ID, 1…65534 |
| 4–5 | Byte offset into UTF-8 JSON message |
| 6–7 | Total UTF-8 byte length, 1…4096 |
| 8… | Payload bytes; absent for kind 2 and idle mailbox |

One client connection and one request in flight. JSON may split anywhere, including within a multibyte UTF-8 character. Reassemble bytes before decoding. Request fragments must have contiguous offsets and the same ID/total. Exact duplicates are accepted; changed overlaps, gaps, invalid kinds/versions, oversize frames/messages, and invalid offsets cause disconnection. The receiver is statically bounded to 4096 bytes plus a NUL terminator. JSON nesting is limited to six. Embedded NULs in string patches are rejected. An incomplete message expires after ten seconds between fragments. Disconnect discards incomplete/queued work and private mailbox data. Operations already started may finish independently.

1. Write each kind-0 fragment **with response**, waiting for the ATT completion before sending the next. ATT completion only confirms transport receipt, never persistence.
2. Read Response. While work is queued, it may contain the previous response or idle header `01 01 00 00 00 00 00 00`. Ignore a different ID and retry reading after 200 ms. Do not send another operation.
3. The matching response begins at offset 0. Repeated reads return the same bytes. For additional fragments, write kind-2 with the same ID/total and offset equal to the number of response bytes received; then read again. This is pull-based backpressure: the clock never floods the phone.
4. When all bytes have arrived, validate JSON `v`, `id`, and `state`. No final offset ACK is necessary. The next request replaces the response.

Clients allow 45 seconds for an operation response; an initial ATT security request also gets up to 45 seconds for iOS pairing. On timeout close the connection, report an **unconfirmed outcome**, reconnect and inspect settings/status. Do not show success based on an ATT write callback.

IDs increase strictly within a connection. Firmware caches the most recently executed ID, a request fingerprint, and its response. An identical retry of that ID replays the response; changed content or an older ID disconnects. Wrap requires reconnecting. A client must not automatically replay `wifi.trial`, `wifi.scan`, `ota.check`, `ota.install`, or settings writes across a disconnect/stack restart. Their outcome may already be applied. Read current state first, then let an explicit user action create a new operation. This bounds replay state without claiming durable exactly-once delivery across reboot. Read requests can be issued afresh after reconnection.

## Messages and operations

Requests are objects: `{"v":1,"id":2,"op":"settings.patch","patch":{"brightness":77,"auto_brightness":false}}`.

Response: `{"v":1,"id":2,"state":"applied","saved":true}`. A subsequent `settings.get` confirms `settings.brightness` equals 77. `queued` means a Wi-Fi/OTA/scan operation was admitted, not completed. `failed` includes a stable `error` and optional `field`. Settings are never acknowledged as saved before the journal, legacy NVS mirrors, and journal cleanup succeed. A persistence failure can leave a complete candidate applied in RAM with recovery pending; `configuration_saved=false` and no saved acknowledgment distinguish that state.

| Operation | Input | Result |
|---|---|---|
| `hello` | none | Identity, firmware, hardware, capabilities, limits |
| `schema.get` | `page`: integer from 0 | Up to 16 fields, `more` boolean |
| `settings.get` | none | `settings` object, `saved` boolean |
| `settings.patch` | `patch` object | `applied`, `saved:true`, or failure |
| `status.get` | none | `status`, including Wi-Fi, provider, memory, bonds and OTA |
| `wifi.scan` | none | `queued`; scan uses clock radio |
| `wifi.results` | none | `scanning`, up to 24 networks (`ssid`, `rssi`, `enterprise`, raw `auth`) |
| `wifi.trial` | `patch` of Wi-Fi keys | `queued`; poll status for completion |
| `ota.check` | none | Queues the existing trusted signed-manifest check |
| `ota.install` | none | Queues the already verified available release, subject to safety checks |
| `bonds.reset` | none | `failed`, `physical_action_required` |

No BLE firmware transfer or arbitrary firmware URL input exists. OTA releases retain the existing signature, SHA-256, hardware/channel/version/partition checks and rollback policy. Bluetooth is temporarily stopped and its allocations released before the existing OTA worker starts, after a 1.5-second acknowledgment opportunity and after active glucose HTTPS finishes. During that interval app progress may be unavailable; the clock displays update progress. Reconnect afterward, read current version and `pending_verification`, and inspect failures/rollback. BLE automatically reinitializes after failures/deferrals/checks; it does not permanently block scheduled updates.

`schema.get` describes supported non-Wi-Fi fields: key, type (`bool`, `int`, `text`, `secret`), integer `min`/`max` or text `max_length`. `src/config_patch.cpp` is the authoritative shared schema. Ranges include brightness 1–255, hours 0–23, poll seconds 15–3600, snooze 1–120 minutes, and auto-cycle 3–300 seconds. Thresholds and alert limits are stored as integer mg/dL, regardless of display units. Convert edited mmol/L values using 18 mg/dL per mmol/L; merely viewing a converted value must not write it back. Threshold ordering and alert-low < alert-high are validated together.

Secrets (`wifi_password`, `wifi_eap_password`, `dexcom_password`, `auth_token`, `weather_api_key`, `server_url`) are omitted from ordinary reads. Instead return `<key>_configured`. URLs are secret because they can embed tokens. Patch semantics: **omitted = unchanged; string = replace (including empty); JSON null = clear** for secret fields. The legacy web adapter retains its empty-string-means-unchanged convention. No app-owned credentials are persisted; only peripheral identities, nicknames and an expected update version are stored on the phone. If future features persist secrets, use Keychain, not preferences.

Wi-Fi keys are accepted only by `wifi.trial` over BLE. Start from a copy of current configuration; omit enterprise settings to retain them. Explicit security `0` selects personal/open; `1` selects enterprise, with EAP method `0` PEAP or `1` TTLS, identity, password, anonymous identity and stored-CA validation. SSIDs are 1–32 bytes, including hidden SSIDs; no auto-downgrade from enterprise occurs. CA upload is not a v1 BLE capability; preserve/use an existing certificate or upload through existing web settings. EAP-TLS is unsupported. A trial is committed only after association and a nonzero DHCP address. Authentication, missing AP, timeout, and save failure are distinct results. Failure resumes the saved network's retry path even if it is presently unavailable. Data-source credentials are never cleared by Wi-Fi replacement or Improv.

`status.trial`: `idle`, `associating`, `authenticating`, `connected`, `failed_auth`, `failed_no_ap`, `failed_timeout`, `failed_save`. `trial_detail` explains the result. `network_saved` and `configuration_saved` are separate from `wifi`. DNS and provider-reachability probes are 0 unknown, 1 available, 2 failed; these do not prove glucose authentication. `provider_http`, `provider_failures`, `data_received`, and `data_age_ms` separately describe provider results. `data_received` means a reading has arrived for the active source; inspect age too. Demo source 2 explicitly generates synthetic readings.

## Errors and recovery

Stable errors: `unsupported_protocol`, `unsupported_operation`, `unsupported_field` (as a validation field), `invalid_json`, `invalid_patch`, `invalid_page`, `validation`, `use_wifi_trial`, `wifi_credentials_or_ca_required`, `persistence_failed`, `busy`, `deferred`, `no_update`, `internal`, `response_too_large`, `physical_action_required`. Validation `field` is a key or `threshold_order` / `alert_order`. Pairing/ATT/disconnect errors are transport errors, not JSON successes. Unknown error codes should remain visible and offer reconnect/troubleshooting.

Shared fixtures are in `fixtures/frames.json` and `fixtures/messages.json`; C++ and Swift tests consume framing fixtures. Host authorization predicates and fault-injected journal tests do not prove real radio authentication or NVS power-cut behavior. Use the physical acceptance checklist before distribution.

## Network memory coordination on the 4 MB clock

`network.pause` indicates that Bluetooth may temporarily disconnect while the clock performs a TLS request. Physical TC001 testing found that keeping NimBLE allocated during Dexcom authentication exhausted heap. The main-loop scheduler now gives a GATT exchange up to 2.5 seconds to finish (or a pending passkey pairing up to 30 seconds), suspends BLE, then runs the network request. Glucose, scheduled weather, reachability probes and fleet requests share an exclusive reservation; OTA waits for it. BLE resumes after completion without removing bonds or settings. The boot nonce remains stable during these pauses.

Clients should reconnect to the saved peripheral while foreground, reauthenticate through the platform bond, verify full device identity, and refresh settings/status. The iOS app makes bounded reconnection attempts and retains unsaved editor drafts. It never resends a mutation automatically across sessions. An interrupted save remains unconfirmed until readback; the editor retains its error and draft. These pauses do not stop the glucose/display/alert engines. Active-phone pairing/reconnection and alert-latency stress checks still require physical acceptance.
