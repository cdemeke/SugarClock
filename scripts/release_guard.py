#!/usr/bin/env python3
"""Fail-closed checks around the legacy GitHub Latest update offer."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import urllib.request

import ota_manifest as ota


def latest_snapshot(repository):
    # Read the same URL compiled into legacy firmware, not just release metadata.
    release = json.loads(subprocess.check_output(
        ["gh", "api", f"repos/{repository}/releases/latest"], text=True))
    url = f"https://github.com/{repository}/releases/latest/download/ota-manifest.json"
    with urllib.request.urlopen(url, timeout=30) as response:
        raw = response.read(ota.MAX_MANIFEST_BYTES + 1)
        expected = f"/releases/download/{release['tag_name']}/"
        # GitHub asset redirects end on a CDN, so validate bytes against the
        # immutable tagged asset as well as the metadata captured above.
    with urllib.request.urlopen(
        f"https://github.com/{repository}{expected}ota-manifest.json", timeout=30
    ) as response:
        tagged = response.read(ota.MAX_MANIFEST_BYTES + 1)
    if len(raw) > ota.MAX_MANIFEST_BYTES or raw != tagged:
        raise ValueError("Latest manifest is oversized or differs from its tagged artifact")
    manifest = json.loads(raw)
    return {"tag": release["tag_name"], "manifest_sha256": hashlib.sha256(raw).hexdigest(),
            "version": manifest["version"]}


def assert_unchanged(before, after):
    if before != after:
        raise ValueError("GitHub Latest changed: stop publishing and investigate the legacy update offer")


def verify_bridge(directory, tag, repository, public_key, previous_version):
    if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
        raise ValueError("bridge must use a stable vMAJOR.MINOR.PATCH tag")
    manifest = ota.load_manifest(str(Path(directory) / "ota-manifest.json"))
    ota.validate_manifest(manifest, public_key=public_key,
                          current_version=previous_version)
    # All supported signed-OTA legacy versions must be able to install it.
    ota.validate_manifest(manifest, current_version="0.2.0")
    if manifest["version"] != tag[1:]:
        raise ValueError("bridge tag/version mismatch")
    name = f"sugarclock-{tag}.bin"
    if manifest["firmware_url"] != f"https://github.com/{repository}/releases/download/{tag}/{name}":
        raise ValueError("bridge firmware URL must reference its immutable release asset")
    firmware = (Path(directory) / name).read_bytes()
    if len(firmware) != manifest["size"] or hashlib.sha256(firmware).hexdigest() != manifest["sha256"]:
        raise ValueError("bridge firmware bytes do not match the signed manifest")
    return {"tag": tag, "manifest_sha256": hashlib.sha256(
        (Path(directory) / "ota-manifest.json").read_bytes()).hexdigest(), "version": manifest["version"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    snap = commands.add_parser("snapshot")
    snap.add_argument("--output", required=True)
    check = commands.add_parser("check")
    check.add_argument("--expected", required=True)
    bridge = commands.add_parser("verify-bridge")
    bridge.add_argument("--directory", required=True)
    bridge.add_argument("--tag", required=True)
    bridge.add_argument("--public-key", required=True)
    bridge.add_argument("--previous", required=True)
    bridge.add_argument("--output", required=True)
    args = parser.parse_args()
    if args.command == "snapshot":
        Path(args.output).write_text(json.dumps(latest_snapshot(args.repository)))
    elif args.command == "check":
        assert_unchanged(json.loads(Path(args.expected).read_text()), latest_snapshot(args.repository))
    else:
        expected = verify_bridge(args.directory, args.tag, args.repository, args.public_key,
                                 json.loads(Path(args.previous).read_text())["version"])
        Path(args.output).write_text(json.dumps(expected))


if __name__ == "__main__":
    main()
