# Publishing and migrating to fleet-controlled updates

Publishing firmware and authorizing a fleet rollout are separate actions. A
preview remains a GitHub prerelease. Promoting its exact tested firmware bytes
creates a stable GitHub release with `--latest=false`; neither operation changes
the legacy update offer or authorizes a fleet rollout. Import the immutable
release into Fleet, select candidate installation IDs, and explicitly mark stable
with a percentage only when ready. The dashboard's stable status does not edit
GitHub releases.

The publishing workflows serialize with bridge promotion and capture both the
GitHub Latest tag and the bytes served by `releases/latest/download/ota-manifest.json`.
They compare that offer with its immutable tagged artifact before publishing and
verify it has not changed afterward. Failed/missing reads stop publishing; there
is deliberately no fallback to creating the first Latest release. Investigate
any guard failure before another publish. A successful artifact publish followed
by a failed guard can leave the new release present; inspect it instead of
rerunning or overwriting artifacts. Independent manual GitHub edits are outside
this workflow lock and must not change Latest during normal operation.

## One-time bridge procedure

1. Keep GitHub Latest pinned to the existing legacy-compatible release. Publish
   the bridge as a candidate, test its exact signed binary on explicitly selected
   physical clocks, then promote those same bytes to a stable non-Latest release.
2. Verify the clock stops independently checking GitHub Latest, remains usable
   when Fleet is unavailable, reports its installation/features, and acknowledges
   a boot-validated update. Exercise pause and offline/reboot recovery. Run the
   legacy/mixed fleet load test and review resource contention on physical clocks.
   Automated server tests alone do not establish physical-device readiness.
3. In GitHub Actions run **Explicitly promote tested bridge to legacy Latest**.
   Supply the existing stable tag and explicitly confirm both completed physical
   and transition-load testing and authorization for all legacy auto-updaters.
   The workflow checks the tag's fleet capability marker, stable release status,
   signed manifest, exact binary size/hash, immutable URL, increasing version,
   and compatibility from the oldest supported signed OTA version (`0.2.0`). It
   then changes only the existing release's Latest status and verifies the offer.
4. This promotion exposes the bridge to **every legacy clock with automatic
   updates enabled**; it is not a 10% rollout. If that is unacceptable, keep the
   old Latest pinned and upgrade clocks manually instead.
5. Leave Latest pinned to the bridge for long-offline devices. Subsequent
   candidate and stable rollouts, including 100%, continue publishing with
   `--latest=false`. Track legacy devices separately until they migrate. Devices
   unable to install the bridge or with automatic updates disabled need a manual
   upgrade.

The bridge workflow does not build or re-sign anything, and does not deploy the
fleet service. Checkboxes attest to external testing; the source capability
marker cannot prove behavior of physical hardware. Do not run bridge promotion
as a routine stable-release operation. A corrective bridge must have a higher
version and repeat the same tests. After any failed promotion verification,
inspect GitHub's current Latest tag and served manifest before retrying: the
Latest edit may already have succeeded. Automatic restoration of an older offer
cannot undo updates already installed.

Public source IPs are processed for networking even when absent from the fleet
database. The nginx example disables access logs, overwrites untrusted forwarded
IP headers, and applies bounded shared per-source/global registration limits.
If an upstream proxy is used, configure its trusted real-IP ranges explicitly;
review error logs, upstream logs, location-provider processing and retention
before public deployment. Rate limits protect availability but cannot establish
that a reported installation represents genuine hardware.
