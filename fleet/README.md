# SugarClock fleet service

Manage 1,000 active clocks with automatic registration, a private dashboard,
configuration snapshots, and signed firmware rollouts. The service stores metadata;
firmware downloads remain on immutable GitHub Release URLs.

## Try the dashboard locally

```bash
python3 -m venv .venv-fleet
. .venv-fleet/bin/activate
pip install -r fleet/requirements.txt
python -m fleet.demo
```

Open http://127.0.0.1:8081/admin/overview. The disposable demo includes 24 synthetic
clocks, nicknames, feature counts, and sample releases. Try a candidate on “My desk,”
pause/resume it, and expand the sample stable rollout from 10% to 100%. Changes
vanish when the process exits. It binds only to loopback, rejects real device API
requests, and cannot import real releases. It is not a production login mode.

## Production setup

Copy `fleet/.env.example` to `fleet/.env`, set independent random secrets, configure
GitHub OAuth and the administrator allowlist, then run:

```bash
docker compose -f fleet/compose.yaml up --build
```

Use the OAuth callback `https://fleet.sugarclock.com/auth/callback` (or your configured
public hostname). Compose binds port 8080 only on loopback. Terminate verified HTTPS
at the proxy; set `FLEET_TRUSTED_PROXY_HOPS` to the exact trusted topology. The nginx
example bounds registrations globally and per source without gating authenticated
check-ins. The service uses one Gunicorn process, eight threads, and SQLite WAL.

No enrollment window or individual approval is needed. Existing pending clocks
become reportable on migration; retired/blocked installations remain disabled.
Use the installation ID in local clock settings to find the corresponding row and
save a private nickname. Renaming never changes deployment membership.

`FLEET_MAX_RECORDS=10000` is a storage ceiling, separate from the 1,000-active-clock
validation target. Remove obsolete `FLEET_ENROLLMENT_REQUIRED` and
`FLEET_ENROLLMENT_MAX_DEVICES` settings. Registration rate limits remain separately
configurable. The dashboard warns at 80% record capacity. Registration-only records
older than 24 hours are cleaned up only for registrations created after the
cleanup-preservation migration. Existing identities/nicknames and blocked/retired
records are preserved without inventing activity timestamps. No
self-registration system can prove that every reporting client is physical hardware.

## Release workflow

1. Publish/synchronize signed release metadata. Publishing alone never starts a
   rollout and normal workflows do not change GitHub Latest.
2. On Releases, select a candidate and explicitly choose test clocks by nickname
   and installation ID. Only fleet-aware, compatible clocks are eligible.
3. Select the signed stable-channel release, choose “Mark stable,” and enter an
   initial percentage such as 10%. The cohort is fixed and rounds up to a whole
   number of clocks.
4. Observe validated boots, failures, deferrals, and missing contacts. Expand
   manually to 100% after the trial; time alone never expands the rollout.
5. Pause to stop new authorizations. A clock already authorized to start can finish.
   Retry failed clocks explicitly after addressing the problem. Boot rollbacks and
   successful installations are not reset by retry. “Return test clocks to stable”
   ends a candidate assignment without downgrading installed firmware.

Temporary manifest connection/read errors before authorization defer automatically
until the next heartbeat; invalid signatures or permanent manifest errors remain
failures. In local clock settings, “Install assigned release now” permits one
manual attempt with automatic updates off or outside the nightly window. It still
requires the current fleet assignment, fresh authorization, and local safety checks.

A newer corrective release can supersede a bad partial rollout. Clocks outside the
new trial retain the last completed stable target. Corrective versions must be
strictly higher; selecting an older release does not downgrade firmware.

**Older firmware cannot enforce percentages.** Follow
[the bridge migration procedure](../docs/fleet-release-migration.md) before using
controlled public rollouts. Test the exact signed bridge on physical clocks, then
explicitly promote it to GitHub Latest for legacy auto-updaters. That one-time step
is broad distribution, not a 10% trial. Latest remains pinned to the bridge while
future releases use fleet targeting. This PR does not promote or publish a release.

## Reporting and location

Firmware reports every five minutes with jitter; legacy firmware polls faster.
Overview shows registered installations, online status, 7/30-day activity, firmware
versions, enabled-feature counts, and approximate city/country counts. Device details
show the latest feature snapshot. Unknown fields remain unknown. One person can own
multiple clocks, and erasing storage can create another installation identity.

Snapshots contain only reviewed booleans/categories: configured Dexcom/FreeStyle
Libre/custom source, companion selection, weather, timer and other enabled features.
They contain no glucose values, patient identifiers, credentials, SSIDs, source URLs,
or weather city settings. This is pseudonymous per-installation reporting, not fully
anonymous analytics. Reporting is disclosed in setup and has no product opt-out.
Enabled-feature snapshots do not measure time spent using those features.

Set `FLEET_IP_GEOLOCATION_ENABLED=1` to enable approximate city/country detection.
The existing HTTPS `ipwho.is` lookup receives the public source IP transiently;
SQLite stores only the resulting coarse location. Refresh defaults to weekly.
Location work is bounded, asynchronous, and may fail without affecting check-ins.
IP location can be wrong, particularly with VPNs and mobile networks. Access logging
is disabled in the provided nginx/Gunicorn configuration; also review tunnel/provider
logging and retention. Enabling lookup is a deployment choice, not enabled by default.

Daily aggregate activity is refreshed hourly, with one row per day. Old device
feature/location snapshots are cleared after 90 days without contact. Aggregate
history is retained for one year. Credentials remain for returning clocks. Completed commands expire after 90 days,
audit records after 180 days, and obsolete rollouts after 180 days; current stable,
fallback, and latest candidate assignments are preserved. Backups need their own
appropriate retention policy.

## Validation

```bash
python -m unittest discover -s tests -v
python -m fleet.load_test --devices 1000 --seconds 90 --rate 12
```

The load harness creates an isolated temporary service/database, runs mixed legacy
and fleet-aware clocks during an active rollout, exercises a reconnect burst, and
checks SQLite backup/restore. It never contacts the production fleet. This is a
local service baseline, not an Internet latency or physical-device reliability test.

Firmware checks use `pio run`, `pio run -t buildfs`, and `python scripts/check_layout.py`.
Physical signed OTA, boot rollback, and concurrent glucose/weather resource checks
are required before bridge promotion. Keep signature/hash/hardware checks, A/B boot
validation, battery and local-activity safeguards intact.

Back up SQLite before upgrading (use the SQLite backup API, or stop writes and copy
the database and WAL consistently). After deployment, verify `/healthz`, sign in,
confirm expected device counts, and exercise a test clock before starting a rollout.
