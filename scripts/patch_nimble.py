"""Backport the upstream queued host-timer fix to pinned NimBLE-Arduino 2.5.0.

This post configuration hook runs after PlatformIO resolves dependencies but
before its compilation actions. Only the environment-local library is changed;
unknown source/version combinations fail the build instead of being patched.
"""
import hashlib
import os
from pathlib import Path
import tempfile

UPSTREAM_COMMIT = "e0c8f5a558893197ae60c3606ded01a2dbb88863"
SOURCE = Path("src/nimble/nimble/host/src/ble_hs.c")
ORIGINAL_SHA256 = "c5677503b264738b692d0e688672011206a308d20c5ca1aa5e32a649443bd48a"
PATCHED_SHA256 = "8232c5a8721f8a75dc08dd93a7b7a1690c16a57f0c8f4660029c45e5a27be324"
BEFORE = (b"        ble_npl_callout_stop(&ble_hs_timer);\n"
          b"        ble_npl_callout_deinit(&ble_hs_timer);\n")
AFTER = b"        ble_npl_callout_stop(&ble_hs_timer);\n"


def patched_source(content):
    digest = hashlib.sha256(content).hexdigest()
    if digest == PATCHED_SHA256:
        return content
    if digest != ORIGINAL_SHA256:
        raise RuntimeError("NimBLE host source checksum mismatch; re-audit the shutdown backport")
    if content.count(BEFORE) != 1:
        raise RuntimeError("NimBLE host shutdown patch location changed")
    result = content.replace(BEFORE, AFTER, 1)
    if hashlib.sha256(result).hexdigest() != PATCHED_SHA256:
        raise RuntimeError("NimBLE patched host checksum mismatch")
    return result


def patch_library(library):
    properties = library / "library.properties"
    values = dict(line.split("=", 1) for line in properties.read_text().splitlines()
                  if "=" in line and not line.lstrip().startswith("#"))
    if values.get("name") != "NimBLE-Arduino" or values.get("version") != "2.5.0":
        raise RuntimeError("NimBLE shutdown backport requires pinned NimBLE-Arduino 2.5.0")
    source = library / SOURCE
    original = source.read_bytes()
    result = patched_source(original)
    if result == original:
        return False
    # A failed/interrupted write must never leave a partially patched C file.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=source.parent, prefix=".ble_hs-", delete=False) as out:
            temporary = Path(out.name)
            out.write(result)
            out.flush()
            os.fsync(out.fileno())
        temporary.chmod(source.stat().st_mode & 0o777)
        temporary.replace(source)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return True


def configure(env):
    library = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "NimBLE-Arduino"
    changed = patch_library(library)
    action = "Applied" if changed else "Verified"
    print("[NIMBLE BUILD] " + action + " upstream host-stop fix " + UPSTREAM_COMMIT[:12])


try:
    Import("env")
except NameError:
    pass
else:
    configure(env)
