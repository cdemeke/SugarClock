#!/usr/bin/env python3
"""Generate and verify SugarClock's canonical signed OTA manifest."""

import argparse
import base64
import datetime as dt
import hashlib
import json
import os
import re
import subprocess
import tempfile

MAX_MANIFEST_BYTES = 8192
PRODUCT = "sugarclock"
HARDWARE = "ulanzi-tc001-esp32-4mb"
CHANNEL = "stable"
CHANNELS = ("stable", "preview")
SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
KEY_ID_RE = re.compile(r"^[a-z0-9-]+$")
SIGNED_FIELDS = (
    "product", "hardware", "channel", "version", "minimum_ota_version",
    "size", "sha256", "firmware_url", "key_id",
)


class ManifestError(ValueError):
    pass


def parse_semver(value):
    match = SEMVER_RE.fullmatch(value or "")
    if not match:
        raise ManifestError("invalid_version")
    return tuple(int(part) for part in match.groups())


def canonical_payload(manifest):
    lines = ["sugarclock-ota-v1"]
    lines.extend(f"{field}={manifest[field]}" for field in SIGNED_FIELDS)
    return ("\n".join(lines) + "\n").encode("utf-8")


def validate_manifest(manifest, current_version=None, slot_size=0x1C0000,
                      expected_sha256=None, public_key=None, expected_channel=CHANNEL):
    required = {"schema", "signature", "published_at", *SIGNED_FIELDS}
    if not isinstance(manifest, dict) or not required.issubset(manifest):
        raise ManifestError("missing_field")
    if manifest["schema"] != 1:
        raise ManifestError("wrong_schema")
    if manifest["product"] != PRODUCT:
        raise ManifestError("wrong_product")
    if manifest["hardware"] != HARDWARE:
        raise ManifestError("wrong_hardware")
    if expected_channel not in CHANNELS or manifest["channel"] != expected_channel:
        raise ManifestError("wrong_channel")
    offered = parse_semver(manifest["version"])
    minimum = parse_semver(manifest["minimum_ota_version"])
    if not isinstance(manifest["size"], int) or isinstance(manifest["size"], bool) or manifest["size"] <= 0:
        raise ManifestError("invalid_size")
    if manifest["size"] > slot_size:
        raise ManifestError("firmware_too_large")
    if not SHA256_RE.fullmatch(manifest["sha256"] or ""):
        raise ManifestError("invalid_sha256")
    url = manifest["firmware_url"]
    if not isinstance(url, str) or not url.startswith("https://") or any(c in url for c in " @#\r\n"):
        raise ManifestError("invalid_firmware_url")
    if not KEY_ID_RE.fullmatch(manifest["key_id"] or ""):
        raise ManifestError("invalid_key_id")
    try:
        dt.datetime.strptime(manifest["published_at"], "%Y-%m-%dT%H:%M:%SZ")
    except (TypeError, ValueError):
        raise ManifestError("invalid_published_at")
    if current_version is not None:
        current = parse_semver(current_version)
        if offered <= current:
            raise ManifestError("not_newer")
        if current < minimum:
            raise ManifestError("minimum_version_not_met")
    if expected_sha256 is not None and manifest["sha256"] != expected_sha256:
        raise ManifestError("sha256_mismatch")
    if public_key is not None:
        verify_signature(manifest, public_key)
    return True


def _openssl(args, *, input_bytes=None):
    result = subprocess.run(["openssl", *args], input=input_bytes,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ManifestError(result.stderr.decode("utf-8", "replace").strip() or "openssl_failed")
    return result.stdout


def sign_payload(payload, private_key):
    with tempfile.NamedTemporaryFile() as payload_file:
        payload_file.write(payload)
        payload_file.flush()
        signature = _openssl(["dgst", "-sha256", "-sign", private_key, payload_file.name])
    return base64.b64encode(signature).decode("ascii")


def verify_signature(manifest, public_key):
    try:
        signature = base64.b64decode(manifest["signature"], validate=True)
    except Exception as exc:
        raise ManifestError("invalid_signature_encoding") from exc
    with tempfile.NamedTemporaryFile() as payload_file, tempfile.NamedTemporaryFile() as sig_file:
        payload_file.write(canonical_payload(manifest))
        payload_file.flush()
        sig_file.write(signature)
        sig_file.flush()
        result = subprocess.run(
            ["openssl", "dgst", "-sha256", "-verify", public_key,
             "-signature", sig_file.name, payload_file.name],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
    if result.returncode:
        raise ManifestError("invalid_signature")
    return True


def load_manifest(path):
    with open(path, "rb") as stream:
        raw = stream.read(MAX_MANIFEST_BYTES + 1)
    if len(raw) > MAX_MANIFEST_BYTES:
        raise ManifestError("manifest_too_large")
    try:
        return json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ManifestError("invalid_json") from exc


def generate(args):
    version = open(args.version_file, encoding="utf-8").read().strip()
    parse_semver(version)
    firmware = open(args.firmware, "rb").read()
    published = args.published_at or dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    manifest = {
        "schema": 1,
        "product": PRODUCT,
        "hardware": HARDWARE,
        "channel": args.channel,
        "version": version,
        "minimum_ota_version": args.minimum_ota_version,
        "size": len(firmware),
        "sha256": hashlib.sha256(firmware).hexdigest(),
        "firmware_url": args.firmware_url,
        "key_id": args.key_id,
        "signature": "",
        "published_at": published,
    }
    validate_manifest({**manifest, "signature": "pending"}, expected_channel=args.channel)
    manifest["signature"] = sign_payload(canonical_payload(manifest), args.private_key)
    public_key = args.public_key
    if public_key:
        verify_signature(manifest, public_key)
    encoded = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    if len(encoded) > MAX_MANIFEST_BYTES:
        raise ManifestError("manifest_too_large")
    with open(args.output, "wb") as output:
        output.write(encoded)
    if args.signature_output:
        with open(args.signature_output, "w", encoding="ascii") as output:
            output.write(manifest["signature"] + "\n")


def verify(args):
    manifest = load_manifest(args.manifest)
    validate_manifest(manifest, current_version=args.current_version,
                      slot_size=args.slot_size, public_key=args.public_key,
                      expected_channel=args.channel)
    print(f"valid signed OTA manifest for v{manifest['version']}")


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    gen = sub.add_parser("generate")
    gen.add_argument("--firmware", required=True)
    gen.add_argument("--firmware-url", required=True)
    gen.add_argument("--version-file", default="VERSION")
    gen.add_argument("--minimum-ota-version", default="0.2.0")
    gen.add_argument("--channel", choices=CHANNELS, default=CHANNEL)
    gen.add_argument("--key-id", default="release-2026-01")
    gen.add_argument("--private-key", required=True)
    gen.add_argument("--public-key")
    gen.add_argument("--published-at")
    gen.add_argument("--output", required=True)
    gen.add_argument("--signature-output")
    gen.set_defaults(func=generate)
    check = sub.add_parser("verify")
    check.add_argument("--manifest", required=True)
    check.add_argument("--public-key", required=True)
    check.add_argument("--current-version")
    check.add_argument("--slot-size", type=lambda v: int(v, 0), default=0x1C0000)
    check.add_argument("--channel", choices=CHANNELS, default=CHANNEL)
    check.set_defaults(func=verify)
    args = parser.parse_args()
    try:
        args.func(args)
    except (ManifestError, OSError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
