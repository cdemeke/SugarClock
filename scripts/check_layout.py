#!/usr/bin/env python3
"""Fail builds when installer/version/partition invariants drift."""

import csv
import hashlib
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXPECTED = {
    "nvs": ("0x9000", "0x5000"),
    "otadata": ("0xe000", "0x2000"),
    "ota_0": ("0x10000", "0x1C0000"),
    "ota_1": ("0x1D0000", "0x1C0000"),
    "spiffs": ("0x390000", "0x70000"),
}


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as stream:
        return stream.read()


def fail(message):
    print(f"layout check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


rows = {}
with open(os.path.join(ROOT, "partitions_custom.csv"), newline="", encoding="utf-8") as stream:
    for row in csv.reader(line for line in stream if not line.lstrip().startswith("#")):
        if not row or not row[0].strip():
            continue
        rows[row[0].strip()] = (row[3].strip(), row[4].strip())
if rows != EXPECTED:
    fail(f"partition table differs: {rows!r}")
if int(rows["spiffs"][0], 16) + int(rows["spiffs"][1], 16) != 0x400000:
    fail("partition table does not end at 4 MiB")

version = read("VERSION").strip()
if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", version):
    fail("VERSION is not strict major.minor.patch")
installer = json.loads(read("docs/manifest.json"))
installer_version = installer.get("version", "")
if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", installer_version):
    fail("docs/manifest.json version is not strict major.minor.patch")

# The USB installer intentionally follows the latest published stable release,
# not the source tree's next version. Bind its label to the exact committed
# binaries so a source-version bump cannot silently advertise stale firmware.
installer_artifacts = json.loads(read("docs/installer-artifacts.json"))
if installer_artifacts.get("version") != installer_version:
    fail("installer manifest version differs from installer artifact metadata")
expected_hashes = installer_artifacts.get("sha256", {})
parts = installer.get("builds", [{}])[0].get("parts", [])
part_paths = {item.get("path") for item in parts}
if set(expected_hashes) != part_paths:
    fail("installer artifact metadata does not cover exactly the manifest parts")
for relative_path, expected_hash in expected_hashes.items():
    artifact_path = os.path.join(ROOT, "docs", relative_path)
    if not os.path.isfile(artifact_path):
        fail(f"installer artifact is missing: {relative_path}")
    with open(artifact_path, "rb") as stream:
        actual_hash = hashlib.sha256(stream.read()).hexdigest()
    if not re.fullmatch(r"[0-9a-f]{64}", expected_hash) or actual_hash != expected_hash:
        fail(f"installer artifact hash differs: {relative_path}")

if "SUGARCLOCK_VERSION" not in read("src/main.cpp") or '#define FIRMWARE_VERSION' in read("src/main.cpp"):
    fail("firmware does not consume the authoritative injected version")
for path in ("data/www/device.html", "onboarding/TC001Setup/TC001Setup/Resources/WebUI/www/device.html"):
    if re.search(r">v\d+\.\d+\.\d+<", read(path)):
        fail(f"hardcoded UI version remains in {path}")

public_key = read("keys/ota-release-2026-01-public.pem").strip()
if public_key not in read("include/ota_public_keys.h"):
    fail("committed release public key differs from firmware key")
segments = {item["path"]: item["offset"] for item in installer["builds"][0]["parts"]}
if segments.get("firmware/littlefs.bin") != 0x390000:
    fail("ESP Web Tools LittleFS offset differs from partition table")

swift = read("onboarding/TC001Setup/TC001Setup/Helpers/FirmwareManager.swift")
for required in ('"458752"', '"0x390000"'):
    if required not in swift:
        fail(f"Mac installer missing {required}")
for path in ("INSTALL.md", "onboarding/TC001Setup/TC001Setup/Helpers/FirmwareManager.swift"):
    text = read(path)
    for obsolete in ("0x210000", "0x1F0000", "2031616", "2162688"):
        if obsolete in text:
            fail(f"obsolete layout value {obsolete} in {path}")

firmware = os.path.join(ROOT, ".pio", "build", "esp32dev", "firmware.bin")
littlefs = os.path.join(ROOT, ".pio", "build", "esp32dev", "littlefs.bin")
if os.path.exists(firmware) and os.path.getsize(firmware) > 0x1C0000:
    fail("firmware.bin exceeds OTA slot")
if os.path.exists(littlefs) and os.path.getsize(littlefs) != 0x70000:
    fail("littlefs.bin does not match filesystem partition")

sdkconfig = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32/sdkconfig")
if os.path.exists(sdkconfig) and "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" not in open(sdkconfig, encoding="utf-8").read():
    fail("resolved ESP32 bootloader does not enable app rollback")

print(f"layout checks passed for source v{version}; USB installer publishes v{installer_version}")
