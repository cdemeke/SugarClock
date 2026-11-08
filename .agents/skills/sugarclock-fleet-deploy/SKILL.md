---
name: sugarclock-fleet-deploy
description: Build signed SugarClock firmware candidates, deploy through Fleet Manager, promote tested candidates to stable, expand rollouts, and report live device update status. Use for SugarClock firmware delivery rather than deployment of the Fleet web service.
---

# SugarClock Fleet deployment

Use https://fleet.sugarclock.com/admin/releases for deployment and
`/admin/devices` and `/admin/overview` for device health. Repository:
`cdemeke/SugarClock`.

## Scope and source of truth

Identify whether the request is to build a candidate, target a test clock,
promote to stable, expand an existing rollout, or read status. A status request
is read-only. Authorization to deploy to a dev clock does not authorize a stable
fleet rollout. Carry explicit rollout authorization forward within the session.

Read the current workflow and implementation before publishing or changing a
rollout; this skill describes operational intent, not a frozen API contract:

- `.github/workflows/preview-release.yml`: signed candidate build.
- `.github/workflows/release.yml`: promotion of tested candidate bytes.
- `docs/fleet-release-migration.md`: Latest pinning and legacy bridge invariants.
- `fleet/README.md`, `fleet/sugarfleet/admin.py`, and
  `fleet/sugarfleet/rollouts.py`: dashboard, APIs, eligibility, and rollout counts.

Use Google Chrome and the existing signed-in profile for browser work, per the
repo owner's preference. Use available supported browser tools; native Chrome
accessibility control can be a fallback when the browser connection fails.
Do not silently switch to an in-app browser. If neither Chrome path works,
report the exact blocker and label any historical counts with their observation
date. An unavailable preview alone does not establish that Chrome is unavailable.
Never present cached counts as a live check. Do not copy cookies or credentials
out of Chrome to work around authentication.

## Build and test a candidate

1. Inspect git status, the intended source commit, `VERSION`, existing tags and
   GitHub releases. Preserve unrelated work. Confirm the requested feature is in
   the selected commit. The device needs a strictly newer firmware semver;
   another preview suffix for the same version does not make it upgradeable.
2. If needed, make a focused VERSION bump, record its commit, and create an
   unused immutable `preview-v<version>-<positive integer>` tag. Push only the
   intended ref. A local version bump is not automatically merged into main.
3. Let `preview-release.yml` perform its tests, production firmware/filesystem
   builds, layout checks, signing, and publication. Verify the workflow succeeded.
   Inspect partial publication before retrying a failed run; never overwrite
   published candidate tags or binaries.
4. Verify the signed `ota-manifest.json` using `scripts/ota_manifest.py` and the
   repository public key. Download the firmware and compare its size and SHA256
   with the signed manifest. Record tag, source SHA, workflow URL, release URL,
   version, binary size, and hash. Never expose signing secrets.
5. Synchronize/import the immutable release in Fleet Manager. Review errors for
   the intended release rather than assuming an unrelated import error invalidates it.

## Target and validate a physical clock

Match the selected clock using its permanent installation UUID, nickname,
hardware, current firmware, and local/USB identity when relevant. Do not infer
that every connected clock is the dev device. Historical dev identity from this
thread is **USB test clock**, installation
`7d57f5b0-fbb3-4b10-9394-27373a233487`, Fleet id 9; verify it still matches.
IP addresses and USB paths are transient and must be rediscovered.

Create a candidate deployment for the selected installation only. Read back the
assignment. Prefer normal Fleet delivery. A user-requested immediate manual
install on the test clock may use the clock's supported install action, after
verifying assignment; do not force public clocks or bypass their auto-update
preferences or maintenance windows. Serial inspection can reset the device.

Installation success requires **boot validation reported to Fleet**, not merely
a download, reboot, or version string. Check device health and last error after
reconnection. Confirm the requested UI feature on the physical device when
possible. Record limitations if functional validation needs user observation.

## Promote and expand

Promote the exact tested preview using the current `release.yml` workflow's
`candidate_tag` input (for example, `gh workflow run release.yml --ref main -f
candidate_tag=<tested-tag>`). Verify stable firmware hash and size match the
candidate and verify its stable signed manifest. Do not rebuild for promotion.

Publishing stable and authorizing Fleet rollout are separate steps. Ordinary
candidate/stable publication must preserve GitHub Latest and the legacy update
offer. Never run the explicit legacy bridge promotion merely to release stable
or reach 100%; it has broader consequences described in the migration document.

In Fleet, select the stable release and requested initial percentage, or expand
the existing rollout to the requested percentage. Check current failures,
rollbacks, deferred devices, and contact status before expanding. Investigate
new failures rather than automatically retrying or broadening delivery. Pause
or rollback only within the user's authorized incident scope.

Read back the saved percentage, release, and target count after mutation. If an
action times out, refresh and inspect before repeating it. At 100%, new eligible
clocks can increase the target count beyond the original cohort. Preserve
candidate overrides and device update preferences. 100% authorization does not
mean 100% installation completion.

## Report status

Refresh live results and report the observation time, release/version,
percentage, current targets, boot-validated installs, deferred, pending/waiting
for contact, installing, failed, and rolled back. Waiting for contact is a subset
of pending targets, not an additional count. Distinguish stable rollout results
from the dev candidate; include both only when clearly labeled and unique.
Use the device inventory for the number currently reporting a version; that is
a different measure from historical successful rollout acknowledgments.

Inspect Clock results or device detail when explaining deferrals; do not guess
that all deferrals have the same cause. Auto-update preference is in device
detail telemetry (`maintenance_window.automatic_install`); count disabled,
enabled, and unknown separately, and state that these are last-reported values.
Offline/dormant devices can retain old firmware without an installation failure.

For a comparison with a previous check, use a small chart when supported by the
host and name both observation dates. Keep the final report concise, link the
Fleet page and release when useful, and describe any remaining uncertainty.
Do not embed this thread's changing rollout counts as permanent skill facts.
