# Nightscout support

PR #31 adds a native Nightscout data source. The display-only changes originally
included in that contribution are tracked separately in PR #34. Background
networking remains part of this implementation so Nightscout requests do not
block display rendering or button handling. This overlaps the older networking
proposal #8 and Nightscout proposal #12; those PRs are not automatically merged
or closed by this work.

## Setup

1. Select **Nightscout** in the Blood Sugar settings.
2. Enter the HTTPS site address, such as `https://nightscout.example.org`.
   A reverse-proxy base path is supported. Do not append `/api/...`, a query
   string, a token, or a fragment. Redirects are rejected; enter the final URL.
3. Select **Public site**, **Access token**, or **API secret**. A read-only token
   created in Nightscout Admin Tools is recommended for a private site.
4. Enter the credential for the chosen method. **Save & Test Connection** saves
   these Nightscout settings, selects Nightscout, and fetches a reading. It shows
   the value, direction, age, and whether the reading is stale.

Leaving the credential blank keeps the saved credential when the authentication
method is unchanged. To replace it, enter a new value. To remove it, check the
explicit removal box. Changing authentication methods clears the old credential;
enter a new one for the new method. Missing credentials can be saved, but testing
will explain what is missing. A brief "request in progress" response means an
active network request must finish before saving; retry in a moment.

The clock must have a valid UTC time for certificate validation and reading age.
If time is unavailable, Nightscout reports that it is waiting for synchronization.

## Compatibility and scope

- Source IDs 0 (Custom URL), 1 (Dexcom), and 2 (Demo) remain unchanged. ID 3 is
  reserved for the separate Libre integration; Nightscout uses ID 4. No existing
  source is inferred to be Nightscout. Users of the unmerged source-3 preview
  should reselect Nightscout and enter its separate settings.
- Nightscout has independent URL/authentication/credential preferences. Switching
  to or from Custom URL does not overwrite that source's settings.
- The device only reads SGV entries. Treatments, insulin delivery, uploader
  credentials, and changes to the Nightscout server are outside this feature.
- Access tokens are sent unchanged in the `api-secret` header. API secrets are
  SHA-1 hashed on the device before being sent in that header. Public reads send
  no authentication header. Credentials are never added to the URL or returned
  through configuration/debug responses.
- HTTPS verifies the hostname and certificate chain using the bundled Mozilla
  roots. There is no automatic insecure fallback. Private certificate authorities
  are not supported by this initial settings interface.
- Requests retrieve `/api/v1/entries/sgv.json?count=3`. Responses have a 16 KiB
  body limit, three-entry limit, and bounded JSON nesting. Five-second idle and
  connection timeouts plus a fifteen-second read deadline bound slow headers
  and bodies.

## Reading behavior

The parser requires an SGV array, integral glucose values, and valid
64-bit epoch-millisecond `date` fields. It maps recognized directions to the
clock's existing arrow vocabulary; absent/unrecognized directions remain unknown.
Values below 39 are Nightscout sensor error codes and are excluded from readings
and delta calculations. The newest valid dated entry wins after ordering and deduplication. Conflicting
values at the same timestamp are rejected. Implausible future timestamps are
excluded; no local receive timestamp is substituted for missing sensor time.

Age is calculated from sensor time, then continues increasing locally. Repeated
successful requests for a frozen reading cannot make it fresh. Failed requests
retain the last good reading, which continues aging under the existing stale
indicator and alert-suppression rules. Older responses cannot replace newer data.

A strictly older predecessor within ten minutes supplies initial delta. Without
one, delta is unavailable instead of being presented as zero. Duplicate readings
do not create new history entries. Changes to the active source or its account
settings clear reading/history state after in-flight polling is drained, so old
account data cannot appear under a newly selected account.

## Verification plan

### Automated checks

- Compile and execute the production parser, request/auth helpers, and partial
  settings application on the host. Cover URL boundaries, token vs API-secret
  headers, credential preservation/replacement/removal, all directions, invalid
  JSON/values/dates, duplicates, ordering, future timestamps, and delta gaps.
- Exercise the actual HTTP integration with fake transport/task primitives:
  frozen reading age, retained data during failures, recovery, initial delta,
  ignored older entries, source resets, and forced Demo requests.
- Run the existing regression suite, ESP32 firmware/filesystem builds, partition
  size checks, and generated-asset reproducibility checks. Both bundled settings
  pages must match.

### Real Nightscout without a personal account

Use `tests/nightscout_integration/` to run a pinned Nightscout release and MongoDB
with synthetic data. No CGM account or patient data is required. Tests exercise
public/private reads, API-secret authentication, actual read-only subject tokens,
rejected credentials, read-only write denial, and feed real server responses into
the production C++ parser. A Mac launcher and Docker configuration make the setup
repeatable. This validates server compatibility; it does not emulate ESP32 TLS,
Wi-Fi, flash storage, display timing, or electrical behavior.

### Physical TC001 release gate (pending)

No TC001 is available during this development session. Before release:

- Compare values, arrows, units, delta, and age against the synthetic server.
- Reboot and verify settings persist; switch between all integrated sources.
- Stop uploads while leaving the server online; verify stale indication and no
  stale glucose alerts. Resume uploads and confirm recovery.
- Disconnect Wi-Fi and exercise wrong credentials, slow responses, and reconnects.
  Confirm buttons/display remain responsive and the device does not reset.
- Verify trusted HTTPS succeeds and untrusted, expired, and wrong-hostname
  certificates fail on the device itself.
- Exercise firmware updates while polling is active and perform an overnight run
  checking watchdog resets, memory use, and continued updates.

## References

- [Nightscout authentication](https://github.com/nightscout/cgm-remote-monitor/wiki/API-v1-Security)
- [Nightscout Admin Tools and read-only roles](https://nightscout.github.io/nightscout/admin_tools/)
- [Nightscout API schema](https://github.com/nightscout/cgm-remote-monitor/blob/master/lib/server/swagger.yaml)
