# SugarClock fleet service

This directory contains the Phase 0/1 remote-management service: device registration and polling, an idempotent command queue, signed GitHub manifest import, a GitHub-allowlisted administrator UI/API, SQLite migrations, and a fake-clock simulator. Firmware integration starts in Phase 2; this service does not replace the signed OTA executor or host firmware bytes.

## Local development

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r fleet/requirements.txt
export FLEET_DATABASE="$PWD/fleet-dev.db"
export FLEET_INSECURE_COOKIES=1
export FLEET_GITHUB_CLIENT_ID=...
export FLEET_GITHUB_CLIENT_SECRET=...
export FLEET_GITHUB_ALLOWLIST=your-github-login
export FLEET_SECRET_KEY="$(openssl rand -hex 32)"
python -m fleet.sugarfleet
```

Register the OAuth callback as `http://127.0.0.1:8080/auth/callback`. For production, set `FLEET_PUBLIC_BASE_URL` to the HTTPS tunnel URL and leave secure cookies enabled.

In another terminal, create three persistent fake clocks:

```bash
python fleet/simulator.py --server http://127.0.0.1:8080 --count 3
```

The simulator state contains device credentials; keep it private and do not commit it.

## Docker Compose

Copy `fleet/.env.example` to `fleet/.env`, fill every secret, then run:

```bash
docker compose -f fleet/compose.yaml up --build
```

The service binds only to loopback. Terminate TLS and expose it through the stable HTTPS tunnel named by `FLEET_PUBLIC_BASE_URL`. Back up the named SQLite volume before upgrades. The service uses one Gunicorn process because SQLite writes are serialized; threads comfortably cover the plan's maximum 250 devices.

## Security boundaries

- The database stores salted PBKDF2-SHA256 device credential hashes, never raw credentials.
- GitHub OAuth accepts only names in `FLEET_GITHUB_ALLOWLIST`; mutations also require CSRF tokens.
- Release sync downloads and verifies signed manifest metadata only. Firmware remains on immutable GitHub Release URLs.
- Secret and connectivity-related configuration fields are rejected in Phase 1, so credentials, SSIDs, enterprise identities, and source URLs are never queued in plaintext. Phase 4 must add external-key encryption and transactional Wi-Fi recovery before enabling those fields.
- Device payloads are capped at 32 KiB. Telemetry is a current snapshot; there is no heartbeat-history table and no source IP persistence.

Run all host tests with `python -m unittest discover -s tests -v` after installing `fleet/requirements.txt`.
