# SugarClock OTA signing

Stable OTA releases use RSA-2048 with SHA-256 and PKCS#1 v1.5. The firmware trusts the public
key identified as `release-2026-01`; the corresponding private key must never be committed,
attached to a release, copied into a build artifact, or printed in CI logs.

## Initial key setup

Generate the key on a trusted, encrypted machine and keep permissions owner-only:

```bash
mkdir -p ~/.config/sugarclock
chmod 700 ~/.config/sugarclock
openssl genrsa -out ~/.config/sugarclock/ota-signing-key.pem 2048
chmod 600 ~/.config/sugarclock/ota-signing-key.pem
openssl rsa -in ~/.config/sugarclock/ota-signing-key.pem -pubout \
  -out keys/ota-release-2026-01-public.pem
```

Copy the same public PEM into `include/ota_public_keys.h`. `scripts/check_layout.py` fails if
the standalone and compiled copies differ. Store an encrypted offline backup of the private
key, document who can access it, and test restoring that backup before the first release.

Create a protected GitHub Actions repository secret named `OTA_SIGNING_KEY_B64`. On macOS:

```bash
base64 -i ~/.config/sugarclock/ota-signing-key.pem | tr -d '\n' | pbcopy
```

On Linux, use `base64 -w0`. Paste the result as the secret value. Do not paste it into an
issue, task, workflow file, shell history argument, or release description.

## Publishing

1. Set `VERSION` to strict `major.minor.patch` and merge the release commit.
2. Tag that exact commit as `v<version>` and push the tag.
3. The signed-release workflow installs the pinned PlatformIO version, runs host tests, builds
   firmware/filesystem/installer binaries, enforces the 1.75 MiB slot, signs the canonical
   manifest, verifies it with the committed public key, and creates the GitHub Release.
4. The immutable firmware URL uses the version tag. GitHub's `releases/latest/download` URL
   exposes the stable `ota-manifest.json` to devices.

The workflow fails if the signing secret is absent or if its private key does not match the
public key compiled into firmware. Temporary private-key files are removed by a shell trap.

## Rotation

Generate a new key and assign a new `key_id`. First publish an update signed by the old key
whose firmware trusts both the old and new public keys. Only after that version is deployed
may releases switch to the new private key/key ID. Keep the old public key compiled long
enough for devices that skipped releases, then retire it in a later bootstrap version.
Compromise of the current private key requires stopping releases, protecting the repository,
and distributing a USB bootstrap that removes the compromised trust anchor if a safe
old-key-signed transition is impossible.
