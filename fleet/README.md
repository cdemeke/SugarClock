# SugarClock fleet service

This directory contains the remote-management service: controlled device enrollment and polling, editable administrator-owned device nicknames and location labels, an idempotent command queue, signed GitHub manifest import, a GitHub-allowlisted administrator UI/API, SQLite migrations, and a fake-clock simulator. The service never proxies or replaces firmware bytes.

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
export FLEET_DEVICE_CREDENTIAL_PEPPER="$(openssl rand -hex 32)"
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

For a same-LAN hardware test, explicitly replace the primary endpoint for that build. Insecure HTTP is rejected unless the test-only opt-in is also present:

```bash
SUGARCLOCK_FLEET_BASE_URL=http://192.168.1.89:8080 \
SUGARCLOCK_FLEET_ALLOW_INSECURE=1 \
platformio run --target upload --upload-port /dev/cu.usbserial-3110
```

Local endpoint overrides are only for USB-flashed development builds and are never present in release candidates. Remote operation requires the stable production hostname to terminate TLS and tunnel to this service. Managed installs still require an immutable HTTPS manifest whose signature, version, channel, firmware hash, size, and ESP image metadata all validate before the inactive A/B partition becomes bootable.

The firmware currently executes notifications, restart, channel, maintenance-window, pause, managed-install, check, and a conservative subset of non-secret configuration commands. Unsupported commands fail explicitly; secret/connectivity patches and manual rollback remain later-phase work.

## Docker Compose

Copy `fleet/.env.example` to `fleet/.env`, fill every secret, then run:

```bash
docker compose -f fleet/compose.yaml up --build
```

The service binds only to loopback. Terminate TLS and expose it through the stable HTTPS tunnel named by `FLEET_PUBLIC_BASE_URL`. Back up the named SQLite volume before upgrades. The service uses one Gunicorn process because SQLite writes are serialized; threads comfortably cover the plan's maximum 250 devices.

`fleet/nginx.conf.example` shows the required forwarding headers and a registration-specific public rate limit. Adjust `FLEET_TRUSTED_PROXY_HOPS` if more than one trusted proxy sits between nginx and the app.

New enrollment is closed by default. On the Devices page, choose **Open for 15 minutes**, power on or flash the new clocks, then approve each pending device. Close enrollment when finished. The service also applies an in-process per-IP registration limit and a hard fleet-size cap; configure matching rate limits at the public reverse proxy.

### Optional approximate city detection

Set `FLEET_IP_GEOLOCATION_ENABLED=1` to resolve a remotely connecting clock's public source IP to an approximate city, region, and country code. Results refresh at most weekly and are labeled approximate in the UI. Administrator labels take precedence over approximate IP location; the installation ID prefix is used when no nickname exists. Private/local addresses cannot be geolocated. Lookups run outside the check-in request path through a bounded queue with a provider circuit breaker.

The lookup uses the HTTPS `ipwho.is` endpoint. Only city, region, and country code are retained; the source IP, coordinates, and postal code are not stored. Enabling this feature discloses the source IP transiently to the lookup provider. For the supported production topology, set `FLEET_TRUSTED_PROXY_HOPS` to the exact number of trusted proxy hops (normally `1`); geolocation refuses to start with zero trusted hops outside tests.

## Security boundaries

- The database stores server-keyed HMAC-SHA256 digests of random 256-bit device credentials, never raw credentials. Legacy PBKDF2 records upgrade after one successful authentication.
- `FLEET_SECRET_KEY` is mandatory outside explicitly insecure local development; missing production secrets fail startup.
- Enrollment is time-limited, capacity-bounded, rate-limited, and requires administrator approval before a clock can check in or receive commands.
- GitHub OAuth accepts only names in `FLEET_GITHUB_ALLOWLIST`; mutations also require CSRF tokens.
- Release sync downloads and verifies signed manifest metadata only. Firmware remains on immutable GitHub Release URLs.
- Secret and connectivity-related configuration fields are rejected in Phase 1, so credentials, SSIDs, enterprise identities, and source URLs are never queued in plaintext. Phase 4 must add external-key encryption and transactional Wi-Fi recovery before enabling those fields.
- Device payloads are capped at 32 KiB. Telemetry is a current snapshot; there is no heartbeat-history table and no source IP persistence. Optional coarse geolocation stores only a periodically refreshed city, region, and country code.

Run all host tests with `python -m unittest discover -s tests -v` after installing `fleet/requirements.txt`.

Before each deployment, stop writes briefly and copy the SQLite database plus its WAL files from the named volume to encrypted storage. After deployment, require the container health check to pass, confirm `/healthz`, sign in through GitHub, verify the expected fleet count, and exercise one canary check-in before reopening enrollment or issuing commands.
