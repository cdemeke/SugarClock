# SugarClock fleet protocol v1

The v1 transport supports legacy clocks and the additive `fleet_rollout_v1`
capability. The named definitions in
[`protocol.schema.json`](../fleet/protocol/v1/protocol.schema.json) describe the
message shapes; [fixtures](../fleet/protocol/v1/fixtures) include both legacy and
fleet-aware check-ins. The schema is a definitions library: select the relevant
`$defs` entry when validating a message. Signature, release compatibility, target
membership, authorization, and state-transition checks also run in application
code; JSON shape validation is not deployment authorization.

Outer message objects accept unknown optional fields. Feature snapshots and
capability values are deliberately allowlisted: adding a telemetry field requires
an explicit server/schema change. Unknown command types must not be acknowledged
as successful.

## Transport and identity

Production devices use certificate-verified HTTPS. On first managed boot the
clock creates a canonical random UUID installation ID and a 32-byte random
credential in the dedicated `sugarfleet` NVS namespace. It sends the credential as
`Authorization: Bearer <base64url credential>` on registration and every later
request. The server stores a server-keyed HMAC-SHA256 digest, not the credential.
A reused installation ID with a different credential is rejected. Ordinary
firmware updates preserve identity; erasing that storage creates a new identity.

Registration and check-in do not require administrator approval. The historical
`verification_state` response field remains for compatibility; `verified` means
reporting is enabled, not proof of genuine hardware or ownership. Explicitly
blocked or retired installations are refused. Registration returns no commands.

## Registration and polling

`POST /device/v1/register` accepts `registration`. Success is `201` for a new
installation or `200` for an authenticated repeat request, with a
`registration_response`. New identity creation is rate-limited independently of
authenticated reporting. Registration rate limits return `429`; record capacity
exhaustion returns retryable `503`. Existing reporting is not gated by that quota.

`POST /device/v1/check-in` accepts `checkin` and returns `checkin_response`.
`next_checkin_seconds` is a requested cadence, currently 300 seconds. Fleet-aware
firmware clamps it to 270–330 seconds and adds ±15 seconds of jitter. Legacy
firmware clamps it to 90–150 seconds before the same jitter; a server setting of
300 does not make old clocks poll every five minutes. Firmware schedules its
initial visit after 30 seconds, subject to network/time availability. Failures
retry after one minute, then five minutes, then hourly probes from the third
consecutive failure. Success resets this circuit breaker.

Fleet work runs in a background task and pauses future glucose/weather requests
for its bounded network visit. Connect/read timeouts and response-size limits
bound individual requests; one visit can contain several requests. This is not
a guarantee of zero impact on core traffic, which needs physical-device testing.
A fleet outage leaves the current firmware running; there is no GitHub Latest
fallback in fleet-aware firmware.

## Operational and feature snapshots

A check-in replaces the current operational snapshot. Installation nicknames and
approximate city/country are administrator/service metadata, not clock-supplied
feature fields. Do not include glucose readings, patient identifiers, credentials,
SSIDs, MAC addresses, raw logs, source URLs, configured weather locations, or raw
failure text. The optional location lookup uses the request's network address;
its configuration and disclosure are separate from this wire contract.

New firmware sends `capabilities: ["fleet_rollout_v1"]` and `features` with
`schema_version: 1`. Allowed feature values are:

- `data_source`: `custom`, `dexcom`, `demo`, or `libre`.
- `companion_character`: integer 0–6 (Pip, Boo, Mochi, Sprout, Pebble, Inky, Maple).
- Booleans: `companion_enabled`, `weather_enabled`, `timer_enabled`,
  `stopwatch_enabled`, `notifications_enabled`, `sysmon_enabled`,
  `auto_cycle_enabled`, `countdown_enabled`, `night_mode_enabled`,
  `auto_brightness`, and `time_display_enabled`.

These are configured features, not measurements of time spent using them.
Missing keys are unknown, not false. A legacy check-in without `features` clears
the current feature snapshot instead of retaining stale data from newer firmware.
Feature totals use the dashboard's stated activity window and count installations,
not people. Reporting is built in, has no product opt-out, and is pseudonymous
rather than anonymous. The disclosure is in the
[project website’s installation section](https://sugarclock.com/#install), not in
the device admin or Mac installer. Installing directly through the app does not
present that website notice.

## Release offers and fresh authorization

`update_offer` is absent on older services and is either null or an `update_offer`
object on this service. Only compatible clocks advertising `fleet_rollout_v1`
receive an offer. Publishing/importing release metadata alone creates no offer.
Candidate selection, stable rollout membership, pause, and release compatibility
are evaluated on the server. See the [migration procedure](fleet-release-migration.md)
for the distinct one-time bridge transition of legacy GitHub Latest updaters.

An offer contains `target_id` (attempt UUID), `rollout_id`, immutable HTTPS
`manifest_url`, `version`, signed manifest `channel` (`stable` or `preview`), and
firmware `sha256`. It is not permission to install a cached image indefinitely.
The clock validates the signed manifest, hardware/product, version and hash
binding, local safety and maintenance policy before requesting authorization:

`POST /device/v1/updates/{target_id}/authorize`

Body: `update_authorization_request`, containing `installation_id`; credentials
remain in the authorization header. Success returns `200` with `authorized: true`,
`expires_at`, `manifest_url`, and the full current `update_offer`. The service
atomically rechecks eligibility and records `installing`; the lease currently
expires after 60 seconds. A paused, superseded, completed, or ineligible target
returns `409`. The clock compares every offered release field with the validated
manifest request and rejects any mismatch.

Immediately before `esp_ota_begin`, including after firmware HTTPS redirects, the
clock checks lease expiry, local enablement, maintenance window, and safety again.
An explicit local manual install bypasses only enablement and the maintenance
window for that attempt; server eligibility, lease expiry, signatures and safety
remain mandatory.
Expired or unsafe starts defer until a new offer/authorization is obtained.
Pause prevents subsequent authorizations; a previously authorized operation may
still begin within its lease, and pause cannot retract an installation already
in progress. `installing` therefore means an authorized attempt, not proof that
flash was written. Signature/hash/image validation and A/B boot validation remain
mandatory. Corrective releases must have higher versions; dashboard selection
does not authorize a downgrade.

## Update outcomes and retry

`POST /device/v1/updates/{target_id}/result` accepts `update_result` with status
`deferred`, `installing`, `boot_validated`, `failed`, or `rolled_back`.
`reason` is an optional sanitized identifier; `code` is a compatibility alias,
with `reason` taking precedence. `boot_validated` requires `firmware_version`
matching the target release. `installing`, `boot_validated`, and `rolled_back`
require a previously recorded authorization. Firmware success requires the
running image to be bootloader `VALID` and no longer pending verification; merely
checking in on a version is not success.

The clock persists the attempt UUID, version/channel, authorization-start marker,
and deferred/failure outcome across reboot. A reboot before authorization defers;
an interrupted authorized attempt fails unless boot state establishes validation
or rollback. Known transient transport/resource errors before authorization are deferred,
including TLS/connect/read failures and HTTP 408/425/429/5xx responses. Invalid
manifest formats, signatures, hash bindings, and permanent HTTP errors remain
failed rather than retrying indefinitely. Deferred attempts can be offered again. Failed attempts require an
administrator retry, which creates a fresh target UUID. No prior release or
candidate authorization grants access to later releases.

Successful outcome receipts return `200` with `recorded` or `already_recorded`.
Repeating the same terminal status is idempotent; a different terminal status
returns `409`. For update outcomes, the stored terminal reason is not replaced by
a repeated status. This differs from legacy command-result equality below.
Firmware retains pending outcomes after network errors and retries while normal
reporting continues. A target-result `404` (for example, after administrator retry
replaces the attempt UUID or retention removes it) clears that local receipt so a
new offer can proceed. Outcome reports are authenticated device claims, not
hardware attestation.

## Legacy command transport

Check-in returns at most 16 pending commands; current firmware executes at most
one per visit to bound fleet traffic. Commands carry UUIDs and creation/expiration
Unix timestamps. The service can redeliver unacknowledged commands. There is no
implemented general NVS cache of the last 32 command results and no exactly-once
guarantee: notification/restart side effects can repeat after a lost receipt.
Do not rely on this legacy queue for rollout authorization.

The reserved v1 command names remain `config_patch`, `set_channel`,
`set_maintenance_window`, `ota_check`, `ota_install`, `ota_pause`,
`ota_rollback_previous`, `restart`, and `notify`. Fleet-aware firmware rejects
queued `ota_install` with `requires_rollout_target`; manual previous-slot rollback
is unsupported. `ota_check` relies on the current check-in's offer. Local update
check controls request a fresh fleet check. The explicit “Install assigned release
now” control creates one RAM-only manual intent, consumed by the next fleet visit
and expiring after 60 seconds while waiting. A failed/no-offer visit consumes it;
it is not restored after reboot or carried into later automatic retries. Manual
install can run with automatic updates off or outside the nightly window, but
still obtains a current assigned offer and fresh authorization. Ordinary automatic
checks report `auto_update_disabled` separately from `outside_maintenance_window`.
Neither control fetches Latest or installs a cached manifest. `set_channel` does not select rollout targets.
A restart window override is not an update authorization or safety override.

`POST /device/v1/commands/{command_id}/result` accepts `command_result`: `accepted`,
`deferred`, `succeeded`, or `failed`. Matching terminal status and result fields
are idempotent; conflicting final results return `409`. Secret and connectivity
configuration patches remain prohibited. Legacy type fixtures preserve wire
compatibility, not a claim that every listed type is executable on new firmware.

## Errors and examples

Errors use an HTTP status and
`{"error":{"code":"sanitized_identifier","message":"safe text"}}`.
Responses must not disclose credentials, authorization headers, request bodies,
secret configuration, or source IPs.

`check-in-request.json` demonstrates legacy reporting;
`fleet-check-in-request.json` demonstrates the additive capability and features.
`check-in-response.json` includes a candidate offer;
`check-in-no-offer-response.json` demonstrates null eligibility.
`update-authorization-*.json`, `update-results.json`, and
`update-result-response.json` demonstrate the fresh-authorization/outcome exchange.
