# Real Nightscout compatibility tests

No Nightscout account, CGM connection, or patient data is needed. This harness
starts two genuine Nightscout **15.0.8** instances and MongoDB **7.0.16**, uploads
synthetic readings, and exercises the same C++ URL/authentication builder and
ArduinoJson parser used in firmware.

The upstream source tag is pinned to commit
`92d0834219aa771b5837dbcbf1baeb839a200cf6`. The macOS MongoDB download has a pinned
SHA-256 verified against MongoDB's published checksum. Compose uses exact release
tags. Test services bind only to loopback and use a temporary TLS certificate;
Python explicitly trusts that certificate while retaining hostname verification.

## Build the production-code test executable

Run from the repository root after PlatformIO has installed ArduinoJson 7.4.3
(e.g. following `pio run`):

```sh
c++ -std=c++11 -Wno-deprecated-declarations \
  -Iinclude -I.pio/libdeps/esp32dev/ArduinoJson/src \
  tests/test_nightscout.cpp src/nightscout_logic.cpp \
  -o /tmp/sugarclock-nightscout-test
```

On Linux, add `-lcrypto` to link the host SHA-1 implementation. Firmware uses
mbedTLS; macOS uses CommonCrypto. Both call the production header builder.

## Apple Silicon Mac without Docker

Requires existing Python 3, Git, Node.js 20+ / npm 10+, and OpenSSL. No global
packages or services are installed:

```sh
python3 tests/nightscout_integration/run_local_macos.py \
  --parser /tmp/sugarclock-nightscout-test
```

The launcher clones the pinned upstream source, runs its locked dependency and
bundle build, and downloads MongoDB into an isolated cache under the temporary
directory. Database files are new for every run and deleted afterward. Processes
are stopped even if assertions fail. Source, dependencies and diagnostic logs
remain cached; use `--cache-dir` to choose another location. Ports 27027, 13371,
and 13372 must be unused. The first run downloads several hundred MB.

## Docker alternative

```sh
export NIGHTSCOUT_TEST_TLS_DIR="$(mktemp -d)"
python3 tests/nightscout_integration/make_tls.py "$NIGHTSCOUT_TEST_TLS_DIR"
docker compose -f tests/nightscout_integration/compose.yml up -d
python3 tests/nightscout_integration/run.py \
  --parser /tmp/sugarclock-nightscout-test \
  --ca-file "$NIGHTSCOUT_TEST_TLS_DIR/test.crt"
docker compose -f tests/nightscout_integration/compose.yml down -v
rm -rf "$NIGHTSCOUT_TEST_TLS_DIR"
```

Run the cleanup commands even if tests fail. Do not expose these disposable
services to the network or use their fixed synthetic API secret elsewhere.

## Assertions

- Both servers report the expected real Nightscout version.
- Public reads succeed; denied-mode anonymous reads fail.
- Production request construction keeps credentials out of URLs.
- API-secret SHA-1 and a raw read-only access token in `api-secret` succeed.
- An incorrect token, incorrect secret hash, and unhashed secret fail.
- API responses parse as **110 mg/dL**, rising trend, **+10 mg/dL** delta, and
  the exact synthetic sensor timestamps.
- Re-parsing a frozen response 20 minutes later reports **1,200 seconds** of
  age; successful retrieval does not imply fresh data.
- Read-only tokens and anonymous public users cannot upload readings.
- Revoking the subject immediately makes its token fail.

A temporary read-only subject is removed on success or failure. Fixture entries
remain only in the disposable database until cleanup.

The `Nightscout integration` GitHub Actions workflow runs the Docker variant
when Nightscout production code or this harness changes, and supports manual
dispatch. It needs no repository secrets.

## Recorded execution and limits

On 2026-09-11 America/New_York (2026-09-12 UTC), the full harness and macOS launcher
passed against actual upstream Nightscout 15.0.8 + MongoDB 7.0.16 on Apple Silicon,
including nine grouped checks over HTTPS. Docker execution was not performed on
this machine because no container runtime was installed.

These tests exercise actual server compatibility and production portable code.
They do **not** execute ESP32 HTTPClient/mbedTLS, NVS persistence, display/button
behavior, Wi-Fi recovery, or an overnight hardware run. The temporary test CA is
not added to firmware trust roots. The firmware's public-CA certificate chain
validation and responsiveness still require hardware acceptance testing.
