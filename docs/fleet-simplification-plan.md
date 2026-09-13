# Fleet simplification plan

Status: implemented in PR #37, with local service/UI tests and firmware builds.
Physical signed-bridge and rollback validation remain required before distribution.
See `fleet/README.md` for the runnable demo and `docs/fleet-release-migration.md`
for deployment instructions. This document records the design and acceptance goals.

## Intended experience

Anyone can install SugarClock. Once connected, the clock registers and reports
without an enrollment window or administrator approval. The administrator sees
active installations, firmware adoption, enabled features, and approximate
city/country, and controls which signed release each clock may install.

Design and test for 1,000 clocks. Keep the existing Python service, SQLite database,
polling protocol, GitHub release artifacts, and signed A/B firmware updater.
Do not introduce a message broker or a separate analytics platform initially.

## Decisions from the discussion

- Both public installations and the administrator's clocks join automatically.
- Show fleet-wide feature counts and feature settings for individual clocks.
- Report configured data source (including Dexcom and FreeStyle Libre), companion
  enabled and selected character, weather enabled, and a reviewed list of other
  feature flags. These are configuration snapshots, not proof of actual usage.
- Feature reporting is built in, with no product opt-out. Explain collection in
  setup. This product decision is not a determination of its legal basis.
- Use random installation identifiers. Do not describe linked per-clock records
  as anonymous: the design is pseudonymous.
- Retain approximate city and country, without precise coordinates or location
  history. Location is best effort and may be unavailable or inaccurate.
- Allow editable, administrator-owned nicknames such as "My desk". Show the
  installation ID in local settings so the administrator can match a physical
  clock to its dashboard row. Avoid encouraging personal names in nicknames.
- Candidate releases go only to explicitly selected clocks.
- Publishing or importing a release does not authorize automatic deployment.
- The administrator marks a release stable and sets a rollout percentage, for
  example 10%. Increasing to 100% is a separate manual action after observation;
  elapsed time never expands the rollout automatically.

## Dashboard

Use three primary views: Overview, Devices, and Releases.

Overview shows total registered installations, recently online installations,
active in the last 7 and 30 days, version distribution, and feature counts.
Default feature counts to installations active in the last 30 days and display
that denominator. Unknown values from older firmware remain unknown, not false.
Store daily aggregate counts for trends rather than every heartbeat.

Devices shows nickname or short installation ID, firmware, last seen, update
status, and approximate city/country. Details show the current feature snapshot
and release eligibility. Label telemetry with its last reported time. Keep
partition and heap diagnostics under an advanced section.

Count installations, not people or guaranteed unique physical clocks. A full
storage erase can create a new identity. Never deduplicate using IP addresses.

Releases provides candidate targeting, Mark stable, rollout percentage, and
Pause/Resume controls. Show actual target count alongside the percentage.
Display targeted, waiting for contact, deferred, installing, boot-validated,
failed, and rolled-back counts, plus time since rollout began. An offline clock
is not automatically an update failure. Replace normal JSON entry with forms.

## Registration and reporting

Remove the approval gate from ordinary registration and check-in. Retain unique
device credentials, HTTPS, admin authentication, CSRF protection, request limits,
and bounded registration rate limits. Provide explicit blocking/retirement for
abuse, distinct from connectivity status. Self-registration authenticates repeat
requests but cannot prove a public client is a genuine physical SugarClock;
reporting counts are operational estimates, not hardware attestation.

Treat 1,000 active clocks as the tested workload, not a lifetime registration
quota. Replace the existing all-row cap: retired records, blocked records, and
stale identities must not consume active-fleet capacity. Show active, dormant,
retired, and blocked counts separately. Do not automatically merge identities
created by storage erases or retire a clock merely because it is temporarily off.

Bound storage independently, with a configurable record/storage ceiling and
headroom above the 1,000-active-clock target; start validation with a 10,000-record
ceiling, then tune from measured storage costs. Alert before exhaustion. Delete
registration-only records that never complete a check-in after 24 hours, and
provide an administrator bulk cleanup action for abuse-created records. Preserve
blocked credential/identity tombstones separately so cleanup does not unblock
known identities. Apply retention rules to dormant telemetry without silently
revoking a returning legitimate clock's identity.

Use shared reverse-proxy per-source and global registration limits, bounded
limiter state, and monitoring of registration/check-in anomalies. Rate-limit new
identity creation separately from authenticated check-ins so existing clocks
remain operational during a registration flood. Reaching the storage ceiling
must produce a retryable registration error and an actionable administrator
alert, not silently discard existing devices. Test recovery after cleanup.
Changing the count to active-only or adding rate limits cannot prevent a
persistent attacker from simulating active clocks; document that residual
availability/count-integrity risk instead of treating these controls as proof
of genuine hardware. None of these controls requires ordinary manual approval.

Recommended starting cadence: five minutes with jitter, with prompt reporting
after boot and update completion. At 1,000 clocks this averages about 3.3 regular
check-ins per second after migration. Old firmware clamps the server interval to
90–150 seconds before adding +/-15 seconds of jitter; asking it for 300 seconds
still gives a nominal 150-second interval. Budget roughly 7–11 check-ins per
second for 1,000 legacy devices across the supported nominal intervals, plus
reconnect, registration, and command-result bursts. Test at least 12 sustained
check-ins per second and a mixed-version fleet before the bridge deployment.
Change the firmware clamp and dashboard freshness thresholds together, while
keeping status meaningful for legacy clocks. Keep bounded timeouts and backoff
so fleet outages do not disrupt glucose/weather traffic.

Persist the latest feature snapshot and operational status. Accept only explicitly
allowed categories and booleans. Exclude glucose values, patient identifiers,
names, emails, passwords, tokens, SSIDs, source URLs, weather city settings, and
raw logs. Keep sanitized update outcome codes and bounded rollout history.

City/country should come from an IP lookup, not the configured weather location.
The existing optional provider receives the source IP; a local lookup database
would avoid this disclosure but adds database updates. Choose the lookup method
before enabling it, document it, and review proxy/access logs as well as database
storage. Do not imply that omitting IPs from SQLite removes all IP processing.
Set retention and deletion rules before public deployment; indefinite per-clock
history is not needed for the requested dashboard.

## Release selection and execution

Keep release metadata separate from deployment authorization. Add a small rollout
record and per-installation target/outcome records; reuse the command transport
where practical rather than replacing the complete protocol.

Candidate selection references installation IDs, never nickname strings. Nickname
edits must not change targets. Candidate approval authorizes that candidate, not
all future experimental releases. Candidate devices can return to the stable
track; if their installed version is newer, show that explicitly rather than
silently forcing a downgrade.

For a stable rollout, freeze the eligible cohort when it starts and select a
deterministic subset. Increasing the percentage adds targets without reshuffling
the original group. Show the rounding rule and selected count. Recommended
initial eligibility is compatible, unblocked installations active within 30 days.
At 100%, include compatible returning/new installations. Until then, installations
outside the trial retain the previous stable target. First-ever stable rollouts
must explicitly show when there is no previous stable target.

Use one authoritative target-selection path for managed automatic updates.
The existing GitHub releases/latest scheduler must not bypass rollout membership.
Changing server targeting alone cannot control that path in old firmware.
An unreachable fleet service must not cause fallback to an unrestricted latest
release. Keep running the installed firmware and retry later.

Change `.github/workflows/release.yml` before publishing rollout-controlled
releases: create ordinary releases with `--latest=false` and retain candidate
prerelease status. Fleet "stable" and GitHub "Latest" are separate concepts.
Keep GitHub Latest explicitly pinned to a designated legacy-compatible release;
verify its tag and served manifest after every publish, including later 100%
rollouts. All new fleet offers reference immutable release URLs. A new endpoint
in new firmware alone cannot change the URL compiled into older firmware.

The bridge release is an explicit one-time exception to percentage control for
legacy auto-updaters. First test its exact signed artifacts on selected physical
clocks via USB or explicit managed installs while it is not Latest. Once those
tests and the transition-load checks pass, a separate administrator action may
promote that tested bridge to GitHub Latest. This makes it available to every
legacy clock with automatic updates enabled, not a selectable 10% of that fleet.
Keep Latest pinned to the bridge afterward so long-offline clocks can migrate
without receiving subsequent trial releases. Inventory legacy clocks separately;
clocks that cannot reach the bridge or have auto-update disabled need a supported
manual upgrade. If broad bridge distribution is unacceptable, retain the old
Latest and migrate manually; do not promise percentage control for legacy clocks.

Keep signature, product/hardware, hash, image, battery, local activity, maintenance
window, and boot-validation checks on the device. Count successful deployment
only after boot validation, not merely download or first contact on the version.
Persist delivery/outcome state across reboot and reconcile lost acknowledgments.

Pause stops new installation authorizations; it cannot retract a completed
installation or reliably stop a flash already in progress. Previously delivered
offers need expiry or fresh authorization before execution so paused rollouts do
not continue through stale queued commands. Recovery from a bad stable release
uses automatic boot rollback where applicable or a signed corrective release
with a higher semantic version than the affected installed build. The existing
manifest validator rejects equal or lower versions; choosing an older stable
release in the dashboard is not a recovery mechanism. The currently unsupported
manual rollback control should not be presented as working functionality.

## Implementation order

1. Simplify enrollment and statuses. Migrate existing legitimate pending devices
   into reporting, preserve blocked/retired records, credentials, and nicknames.
   Replace the lifetime row cap with the capacity, cleanup, and abuse controls
   above. Remove enrollment controls and update stale documentation.
2. Add versioned feature snapshots, installation ID display in local settings,
   dashboard totals/details, and nickname display/search that resolves selections
   to installation IDs. Show short IDs alongside duplicate nicknames. Preserve
   compatibility with existing firmware and distinguish missing telemetry.
3. Add candidate targets, stable designation, deterministic rollout membership,
   pause/resume, and outcome tracking. Connect firmware to authoritative release
   targeting and validated-boot acknowledgments.
4. Change and verify release publishing so normal releases cannot advance GitHub
   Latest. Load-test 1,000 legacy/mixed-version devices at the transition rate,
   including bursts and registration abuse. Measure latency and database
   contention, and verify backup/restore and offline recovery before migration.
5. Test the exact signed bridge on selected physical clocks, then use the explicit
   bridge-promotion procedure above. Show which clocks still lack rollout control.
   Keep GitHub Latest pinned to the bridge and verify returning legacy clocks can
   migrate. Confirm fleet-aware clocks respect targeting before subsequent trials.
6. Validate 1,000 migrated devices at five-minute polling, then run a real 10%
   rollout among eligible fleet-aware clocks and manually expand after observation.

## Required validation

- First boot reports without administrator action; existing identities survive
  migration; blocked installations remain blocked.
- Feature counts use the stated activity window and handle old firmware correctly.
- More than 1,000 historical records, including retired/blocked/erased identities,
  do not prevent 1,000 active clocks from reporting or normal new enrollment.
- Test sustained fake registration below the per-IP threshold, global limiting,
  bounded limiter memory, cleanup, alerts, and storage-ceiling recovery. Existing
  authenticated devices keep reporting during the flood. Test shared-network
  installations and acknowledge that simulated active devices remain possible.
- Candidate eligibility is explicit and cannot leak to public devices. Renaming
  a clock or using duplicate nicknames never changes stored installation targets.
- Normal publishing/importing and fleet stable promotion leave GitHub Latest and
  its manifest unchanged. Test this with a legacy updater as well as bridge
  firmware. Only explicit bridge promotion changes the legacy update offer.
- A 10% trial is stable across repeated polls; 100% preserves initial targets and
  adds remaining eligible installations without advancing GitHub Latest.
- The legacy latest-release route cannot bypass targeting on bridge firmware.
  A long-offline legacy clock receives the pinned bridge, then obeys fleet policy.
- Corrective releases use a strictly higher semantic version; equal/lower offers
  fail validation, while automatic boot rollback remains independently supported.
- Pausing blocks new starts, including delayed offers under the defined policy.
- Disconnects, reboots, duplicate messages, deferred installs, failed validation,
  and rollbacks produce accurate outcomes without duplicate execution.
- At 1,000 devices, test legacy, mixed, and migrated populations; sustain at least
  12 check-ins per second for transition sizing and separately exercise reconnect
  and update-result bursts. Five-minute reporting is only the migrated baseline.
  Device fleet traffic must not starve glucose/weather requests; verify device
  resource behavior with hardware tests, not only server load simulation.

## Repository observations

The server currently refuses pending-device check-ins in
`fleet/sugarfleet/device.py`; the retry policy in `src/fleet_policy.cpp` reaches
one hour after three failures. The UI in `fleet/sugarfleet/templates` exposes
raw command JSON and lacks the intended rollout workflow. `src/ota_manager.cpp`
independently checks the GitHub latest-release manifest. `src/fleet_manager.cpp`
already reports operational snapshots and supports signed managed installation.

The configured default enrollment cap is already 1,000 in
`fleet/sugarfleet/__init__.py`; the README's 250-device sizing statement is stale.
The cap is not evidence of tested capacity.
