"""Tripwire for changes to Arduino's early OTA acceptance hook and its linkage."""

from pathlib import Path
import re


def check_ota_boot_validation(framework_source, firmware_map):
    source = Path(framework_source).read_text(encoding="utf-8")
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    if not re.search(
        r"\bbool\s+verifyRollbackLater\s*\(\s*(?:void\s*)?\)\s*"
        r"__attribute__\s*\(\(\s*weak\s*\)\)\s*;", source
    ):
        raise ValueError("Arduino OTA weak hook signature changed; review first-boot validation")
    if not re.search(
        r"\bvoid\s+initArduino\s*\(\s*(?:void\s*)?\)\s*\{.*?"
        r"\bif\s*\(\s*!\s*verifyRollbackLater\s*\(\s*\)\s*\)", source, re.S
    ):
        raise ValueError("Arduino initArduino no longer gates acceptance on verifyRollbackLater")

    # Discarded input sections also name the weak hook. Only the live map and
    # its unmangled symbol prove which implementation the firmware will call.
    contents = Path(firmware_map).read_text(encoding="utf-8")
    _, separator, live = contents.partition("Linker script and memory map")
    if not separator:
        raise ValueError("firmware map is missing its live linker section")
    live = live.split("Cross Reference Table", 1)[0]
    definitions = re.findall(
        r"^\s*\.text\.verifyRollbackLater\s+"
        r"(0x[0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+(\S+)\s*\n"
        r"\s*(0x[0-9a-fA-F]+)\s+verifyRollbackLater\s*$", live, re.M
    )
    if len(definitions) != 1:
        raise ValueError("firmware map must contain exactly one live C verifyRollbackLater definition")
    address, owner, symbol_address = definitions[0]
    if (int(address, 16) == 0 or int(address, 16) != int(symbol_address, 16)
            or not owner.endswith("/src/ota_boot_validation.cpp.o")):
        raise ValueError("live verifyRollbackLater is not owned by SugarClock ota_boot_validation.cpp.o")
