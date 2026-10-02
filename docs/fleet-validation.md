# Fleet implementation validation

Local validation for PR #37, September 12–13, 2026. No production fleet, GitHub
release, or physical clock was modified by these checks.

## Service and dashboard

- Full Python unittest discovery passed, including migration, telemetry privacy,
  candidate isolation, percentage stability, corrective-release fallback, fresh
  authorization, retry attempts, candidate completion, and rendering/CSRF checks.
- Site analytics suite: 11 tests passed.
- Browser review of synthetic demo: overview counts, feature reporting, candidate
  selection by nickname, candidate creation, pause, and manual 10% → 100% expansion.
- Inline dashboard JavaScript parses; nicknames are escaped; the disposable demo
  rejects non-loopback hosts and real device/import routes.

## 1,000-clock service baseline

Command: `python -m fleet.load_test --devices 1000 --seconds 90 --rate 12`

One local Gunicorn worker, eight threads, SQLite WAL, 50% legacy / 50% fleet-aware
clients, active stable rollout. Device identities and a synthetic release catalog
are seeded in an isolated temporary database; requests use real HTTP and device
credential authentication. This does not measure public registration throughput,
Internet latency, or firmware download traffic.

| Check | Result |
| --- | --- |
| Paced reporting | 1,080 requests over 89.93 seconds, 12.01 requests/second |
| Paced errors | 0 |
| Paced p95 latency | 8.74 ms |
| Reconnect burst | 1,000 requests, 16 concurrent clients, 0 errors |
| Burst p95 latency | 29.78 ms |
| Backup/restore | SQLite integrity `ok`, all 1,000 reporting identities restored |

CI runs a shorter ten-second paced phase plus the full 1,000-request burst. The
benchmark is a reproducible local baseline, not a hosting capacity guarantee.

## Firmware and release safety

- PlatformIO firmware build passed: 1,475,328 bytes of a 1,835,008-byte OTA slot.
- Installer filesystem build and `scripts/check_layout.py` passed.
- Embedded web assets reproduced identically after regeneration.
- Release guard tests verify unchanged legacy Latest during normal publishing,
  signed bridge compatibility, immutable artifacts, and explicit bridge promotion.
- Protocol fixtures and additive feature/offer/authorization/result contracts pass
  schema and host-side checks.

Before public bridge promotion, test the exact signed artifacts on a physical
clock: interrupted download, power loss/reboot, failed boot validation and rollback,
expired authorization, maintenance-window behavior, and concurrent glucose/weather
traffic. Simulated success is not physical boot validation. The manual bridge
workflow requires explicit acknowledgement of those checks and broad legacy
migration; ordinary publication never performs that promotion.

## Review locally

Install `fleet/requirements.txt`, then run `python -m fleet.demo` and open
`http://127.0.0.1:8081/admin/overview`. See [fleet README](../fleet/README.md) and
[bridge migration instructions](fleet-release-migration.md).
