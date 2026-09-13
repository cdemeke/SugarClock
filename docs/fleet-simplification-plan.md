# Fleet simplification plan

Status: agreed product direction; implementation has not started.

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

Recommended starting cadence: five minutes with jitter, with prompt reporting
after boot and update completion. At 1,000 clocks this averages about 3.3 regular
check-ins per second. Firmware currently clamps polling to 90–150 seconds;
change the clamp and dashboard freshness thresholds together. Keep bounded
timeouts and backoff so fleet outages do not disrupt glucose/weather traffic.

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

Keep signature, product/hardware, hash, image, battery, local activity, maintenance
window, and boot-validation checks on the device. Count successful deployment
only after boot validation, not merely download or first contact on the version.
Persist delivery/outcome state across reboot and reconcile lost acknowledgments.

Pause stops new installation authorizations; it cannot retract a completed
installation or reliably stop a flash already in progress. Previously delivered
offers need expiry or fresh authorization before execution so paused rollouts do
not continue through stale queued commands. Recovery from a bad stable release
uses automatic boot rollback where applicable or a signed corrective release;
the currently unsupported manual rollback control should not be presented as
working functionality.

## Implementation order

1. Simplify enrollment and statuses. Migrate existing legitimate pending devices
   into reporting, preserve blocked/retired records, credentials, and nicknames.
   Remove enrollment controls and update stale documentation.
2. Add versioned feature snapshots, installation ID display in local settings,
   dashboard totals/details, and nickname-based selection. Preserve compatibility
   with existing firmware and distinguish missing telemetry.
3. Add candidate targets, stable designation, deterministic rollout membership,
   pause/resume, and outcome tracking. Connect firmware to authoritative release
   targeting and validated-boot acknowledgments.
4. Test on selected physical clocks, then distribute the fleet-aware bridge
   firmware through the existing update mechanism. Explicitly show legacy devices
   whose updater cannot yet enforce percentages; do not claim controlled rollout
   for those versions. Verify the transition before releasing subsequent trials.
5. Load-test 1,000 simulated devices, measure latency and database contention,
   verify backup/restore and offline recovery, then run a real 10% rollout and
   manually expand it after observation.

## Required validation

- First boot reports without administrator action; existing identities survive
  migration; blocked installations remain blocked.
- Feature counts use the stated activity window and handle old firmware correctly.
- Registration abuse limits do not prevent normal shared-network installations.
- Candidate eligibility is explicit and cannot leak to public devices.
- Publishing alone offers no update. A 10% trial is stable across repeated polls;
  100% preserves initial targets and adds remaining eligible installations.
- The legacy latest-release route cannot bypass targeting on bridge firmware.
- Pausing blocks new starts, including delayed offers under the defined policy.
- Disconnects, reboots, duplicate messages, deferred installs, failed validation,
  and rollbacks produce accurate outcomes without duplicate execution.
- At 1,000 devices, reporting remains bounded and device fleet traffic does not
  starve glucose/weather requests. Use hardware tests for device resource behavior.

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
