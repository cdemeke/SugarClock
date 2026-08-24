#!/usr/bin/env python3
import os
import re
import sys
import json

try:
    Import("env")
    RUNNING_IN_PIO = True
except Exception:
    RUNNING_IN_PIO = False

ROOT_DIR = (env.subst("$PROJECT_DIR") if RUNNING_IN_PIO
            else os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VERSION_FILE = os.path.join(ROOT_DIR, "VERSION")

def get_version():
    if not os.path.exists(VERSION_FILE):
        raise RuntimeError(f"Missing authoritative VERSION file at {VERSION_FILE}")
    with open(VERSION_FILE, "r") as fp:
        ver = fp.read().strip()

    if not re.match(r'^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$', ver):
        raise ValueError(f"Malformed VERSION '{ver}'. Must be strict major.minor.patch (e.g. 0.2.0)")
    return ver

version_str = get_version()
print(f"[PRE-BUILD] Authoritative SugarClock Version: {version_str}")

# Keep the ESP Web Tools bootstrap manifest synchronized with VERSION. The OTA
# release manifest is generated separately and signed by the release workflow.
installer_manifest = os.path.join(ROOT_DIR, "docs", "manifest.json")
if os.path.exists(installer_manifest):
    with open(installer_manifest, "r") as fp:
        manifest = json.load(fp)
    manifest["version"] = version_str
    with open(installer_manifest, "w") as fp:
        json.dump(manifest, fp, indent=2)
        fp.write("\n")

# Generate flash-embedded web assets
sys.path.insert(0, os.path.join(ROOT_DIR, "scripts"))
from generate_web_assets import generate_web_assets
generate_web_assets()

if RUNNING_IN_PIO:
    # Inject build flags
    env.Append(CPPDEFINES=[
        ("SUGARCLOCK_VERSION", f'\\"{version_str}\\"'),
        ("SUGARCLOCK_HARDWARE_ID", '\\"ulanzi-tc001-esp32-4mb\\"'),
        ("SUGARCLOCK_CHANNEL", '\\"stable\\"')
    ])
