"""Backport the upstream queued host-timer fix to pinned NimBLE-Arduino 2.5.0,
and keep its server from deleting an existing bond on an unadmitted repeat pairing.

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

# NimBLEServer's default repeat-pairing handler deletes the stored bond before
# any application callback runs. Ask the firmware first; it ignores the request
# (keeping the bond) unless the owner opened the physical pairing window.
SERVER_SOURCE = Path("src/NimBLEServer.cpp")
SERVER_ORIGINAL_SHA256 = "af4335cf6b9e5ca3be63770307736fe50d29b53529c0dd3579f7ed515b553895"
SERVER_PATCHED_SHA256 = "c1683a78cb5f0e7b6b97560ba4f941d6d3bb9a3c8d2c7b6e6d7be119851123fb"
SERVER_EDITS = (
    (b'static const char*           LOG_TAG = "NimBLEServer";\n',
     b'static const char*           LOG_TAG = "NimBLEServer";\n'
     b'// SugarClock: the application decides whether a repeat pairing may replace a bond.\n'
     b'extern "C" bool sugarclock_repeat_pairing_allowed(uint16_t conn_handle);\n'),
    (b"            /* Delete the old bond. */\n"
     b"            rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &peerInfo.m_desc);\n",
     b"            if (!sugarclock_repeat_pairing_allowed(event->repeat_pairing.conn_handle)) {\n"
     b"                return BLE_GAP_REPEAT_PAIRING_IGNORE;\n"
     b"            }\n\n"
     b"            /* Delete the old bond. */\n"
     b"            rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &peerInfo.m_desc);\n"),
)


def apply_edits(content, original_sha, patched_sha, edits, label):
    digest = hashlib.sha256(content).hexdigest()
    if digest == patched_sha:
        return content
    if digest != original_sha:
        raise RuntimeError("NimBLE " + label + " source checksum mismatch; re-audit the backport")
    result = content
    for before, after in edits:
        if result.count(before) != 1:
            raise RuntimeError("NimBLE " + label + " patch location changed")
        result = result.replace(before, after, 1)
    if hashlib.sha256(result).hexdigest() != patched_sha:
        raise RuntimeError("NimBLE patched " + label + " checksum mismatch")
    return result


def patched_source(content):
    return apply_edits(content, ORIGINAL_SHA256, PATCHED_SHA256, ((BEFORE, AFTER),), "host")


def server_patched_source(content):
    return apply_edits(content, SERVER_ORIGINAL_SHA256, SERVER_PATCHED_SHA256, SERVER_EDITS, "server")


def write_atomically(source, result):
    # A failed/interrupted write must never leave a partially patched C file.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=source.parent, prefix="." + source.stem + "-", delete=False) as out:
            temporary = Path(out.name)
            out.write(result)
            out.flush()
            os.fsync(out.fileno())
        temporary.chmod(source.stat().st_mode & 0o777)
        temporary.replace(source)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def patch_library(library):
    properties = library / "library.properties"
    values = dict(line.split("=", 1) for line in properties.read_text().splitlines()
                  if "=" in line and not line.lstrip().startswith("#"))
    if values.get("name") != "NimBLE-Arduino" or values.get("version") != "2.5.0":
        raise RuntimeError("NimBLE shutdown backport requires pinned NimBLE-Arduino 2.5.0")
    # Verify every file before writing any, so a mismatch leaves the library untouched.
    pending = []
    for relative, patcher in ((SOURCE, patched_source), (SERVER_SOURCE, server_patched_source)):
        source = library / relative
        original = source.read_bytes()
        result = patcher(original)
        if result != original:
            pending.append((source, result))
    for source, result in pending:
        write_atomically(source, result)
    return bool(pending)


def configure(env):
    library = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "NimBLE-Arduino"
    changed = patch_library(library)
    action = "Applied" if changed else "Verified"
    print("[NIMBLE BUILD] " + action + " upstream host-stop fix " + UPSTREAM_COMMIT[:12] +
          " and repeat-pairing admission")


try:
    Import("env")
except NameError:
    pass
else:
    configure(env)
