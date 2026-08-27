# SugarClock fleet service

This directory contains the remote-management service: device registration and polling, editable administrator-owned device nicknames and location labels, an idempotent command queue, signed GitHub manifest import, a GitHub-allowlisted administrator UI/API, SQLite migrations, and a fake-clock simulator. Firmware v0.2.3 adds the device-side fleet client and a managed entry point into the existing signed A/B OTA executor; the service never proxies or replaces firmware bytes.

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

## Device firmware

Production builds use `https://fleet.sugarclock.com` and require certificate-verified HTTPS. A clock creates a random installation UUID and 32-byte credential in the dedicated `sugarfleet` NVS namespace, registers automatically, and checks in every 90-150 seconds with jitter. Fleet traffic is serialized with glucose/weather TLS traffic to stay within ESP32 heap limits.

For a same-LAN hardware provisioning test, build with an explicit fallback. Insecure HTTP is rejected unless the test-only opt-in is also present:

```bash
SUGARCLOCK_FLEET_FALLBACK_URL=http://192.168.1.89:8080 \
SUGARCLOCK_FLEET_ALLOW_INSECURE=1 \
platformio run --target upload --upload-port /dev/cu.usbserial-3110
```

The fallback is only for initial local validation. Remote operation requires the stable production hostname to terminate TLS and tunnel to this service. Managed installs still require an immutable HTTPS manifest whose signature, version, channel, firmware hash, size, and ESP image metadata all validate before the inactive A/B partition becomes bootable.

The firmware currently executes notifications, restart, channel, maintenance-window, pause, managed-install, check, and a conservative subset of non-secret configuration commands. Unsupported commands fail explicitly; secret/connectivity patches and manual rollback remain later-phase work.

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
