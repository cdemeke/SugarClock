"""Import immutable, signed manifests from GitHub Releases."""

import base64
import json
import os
import re
import subprocess
import tempfile
import urllib.request

from .util import ApiError, SEMVER_RE, SHA256_RE, json_text, now_epoch
from .validation import CHANNELS, immutable_https_url, validate_published_at


SIGNED_FIELDS = (
    "product",
    "hardware",
    "channel",
    "version",
    "minimum_ota_version",
    "size",
    "sha256",
    "firmware_url",
    "key_id",
)


def canonical_payload(manifest):
    return ("sugarclock-ota-v1\n" + "".join(f"{field}={manifest[field]}\n" for field in SIGNED_FIELDS)).encode("utf-8")


def validate_signed_manifest(manifest, manifest_url, public_keys_dir):
    required = {"schema", "signature", "published_at", *SIGNED_FIELDS}
    if not isinstance(manifest, dict) or not required.issubset(manifest):
        raise ApiError("invalid_manifest", "manifest is missing a required field", 422)
    if manifest["schema"] != 1 or manifest["product"] != "sugarclock":
        raise ApiError("invalid_manifest", "manifest schema or product is invalid", 422)
    if manifest["hardware"] != "ulanzi-tc001-esp32-4mb":
        raise ApiError("invalid_manifest", "manifest hardware is invalid", 422)
    if manifest["channel"] not in CHANNELS:
        raise ApiError("invalid_manifest", "manifest channel is invalid", 422)
    if not SEMVER_RE.fullmatch(manifest["version"] if isinstance(manifest["version"], str) else ""):
        raise ApiError("invalid_manifest", "manifest version is invalid", 422)
    if not SEMVER_RE.fullmatch(
        manifest["minimum_ota_version"] if isinstance(manifest["minimum_ota_version"], str) else ""
    ):
        raise ApiError("invalid_manifest", "manifest minimum OTA version is invalid", 422)
    if type(manifest["size"]) is not int or not 0 < manifest["size"] <= 0x1C0000:
        raise ApiError("invalid_manifest", "manifest firmware size is invalid", 422)
    if not SHA256_RE.fullmatch(manifest["sha256"] if isinstance(manifest["sha256"], str) else ""):
        raise ApiError("invalid_manifest", "manifest firmware hash is invalid", 422)
    if not immutable_https_url(manifest_url) or not immutable_https_url(manifest["firmware_url"]):
        raise ApiError("invalid_manifest", "manifest URLs must be immutable HTTPS URLs", 422)
    validate_published_at(manifest["published_at"])
    key_id = manifest["key_id"]
    if not isinstance(key_id, str) or not re.fullmatch(r"[a-z0-9-]+", key_id):
        raise ApiError("invalid_manifest", "manifest key ID is invalid", 422)
    public_key = os.path.join(public_keys_dir, f"ota-{key_id}-public.pem")
    if not os.path.isfile(public_key):
        raise ApiError("unknown_manifest_key", "manifest signing key is not trusted", 422)
    try:
        signature = base64.b64decode(manifest["signature"], validate=True)
    except (TypeError, ValueError) as error:
        raise ApiError("invalid_manifest_signature", "manifest signature encoding is invalid", 422) from error
    with tempfile.NamedTemporaryFile() as payload_file, tempfile.NamedTemporaryFile() as signature_file:
        payload_file.write(canonical_payload(manifest))
        payload_file.flush()
        signature_file.write(signature)
        signature_file.flush()
        result = subprocess.run(
            ["openssl", "dgst", "-sha256", "-verify", public_key, "-signature", signature_file.name, payload_file.name],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    if result.returncode:
        raise ApiError("invalid_manifest_signature", "manifest signature is invalid", 422)
    return manifest


def import_manifest(connection, manifest, manifest_url, administrator, public_keys_dir):
    validate_signed_manifest(manifest, manifest_url, public_keys_dir)
    existing = connection.execute(
        "SELECT * FROM releases WHERE version=? AND channel=?",
        (manifest["version"], manifest["channel"]),
    ).fetchone()
    identity = (manifest_url, manifest["firmware_url"], manifest["sha256"], manifest["size"])
    if existing:
        stored = (
            existing["manifest_url"],
            existing["firmware_url"],
            existing["firmware_sha256"],
            existing["firmware_size"],
        )
        if stored != identity:
            raise ApiError("release_immutable_conflict", "release version already has different immutable assets", 409)
        return existing["id"], False
    cursor = connection.execute(
        "INSERT INTO releases (version, channel, manifest_url, firmware_url, firmware_sha256, "
        "firmware_size, published_at, imported_at, approved_by) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
        (
            manifest["version"],
            manifest["channel"],
            manifest_url,
            manifest["firmware_url"],
            manifest["sha256"],
            manifest["size"],
            manifest["published_at"],
            now_epoch(),
            administrator,
        ),
    )
    return cursor.lastrowid, True


def _read_json(url, token=None, maximum=512 * 1024):
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "SugarClock-Fleet/1"}
    if token:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=20) as response:
        if response.length is not None and response.length > maximum:
            raise ApiError("github_response_too_large", "GitHub response is too large", 502)
        body = response.read(maximum + 1)
    if len(body) > maximum:
        raise ApiError("github_response_too_large", "GitHub response is too large", 502)
    try:
        return json.loads(body)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ApiError("github_invalid_json", "GitHub returned invalid JSON", 502) from error


def synchronize(connection, repository, administrator, public_keys_dir, github_token=None):
    releases = _read_json(f"https://api.github.com/repos/{repository}/releases?per_page=50", github_token)
    if not isinstance(releases, list):
        raise ApiError("github_invalid_response", "GitHub releases response is invalid", 502)
    imported = 0
    unchanged = 0
    errors = []
    for release in releases:
        for asset in release.get("assets", []):
            name = asset.get("name", "")
            if not (name.startswith("ota-manifest") and name.endswith(".json")):
                continue
            url = asset.get("browser_download_url", "")
            try:
                manifest = _read_json(url, github_token, maximum=8192)
                _release_id, created = import_manifest(
                    connection, manifest, url, administrator, public_keys_dir
                )
                imported += int(created)
                unchanged += int(not created)
            except (ApiError, OSError) as error:
                errors.append({"asset": name[:128], "code": getattr(error, "code", "github_unavailable")})
    connection.commit()
    return {"imported": imported, "unchanged": unchanged, "errors": errors}
