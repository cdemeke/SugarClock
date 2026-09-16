import importlib.util
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "check_ota_boot_validation",
    Path(__file__).resolve().parents[1] / "scripts/check_ota_boot_validation.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)

CORE = """
bool verifyRollbackLater() __attribute__((weak));
bool verifyRollbackLater() { return false; }
void initArduino() {
    if (!verifyRollbackLater()) { /* SDK acceptance */ }
}
"""
SECTION = """ .text.verifyRollbackLater
                0x00000000401acb50        0x7 .pio/build/esp32dev/src/ota_boot_validation.cpp.o
                0x00000000401acb50                verifyRollbackLater
"""
MAP = "Linker script and memory map\n" + SECTION


class OtaBootBuildGuardTests(unittest.TestCase):
    def check(self, core=CORE, mapping=MAP):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, firmware_map = root / "core.c", root / "firmware.map"
            if core is not None:
                source.write_text(core)
            if mapping is not None:
                firmware_map.write_text(mapping)
            guard.check_ota_boot_validation(source, firmware_map)

    def test_accepts_live_application_hook_ignoring_discarded_weak_hook(self):
        discarded = SECTION.replace("0x00000000401acb50", "0x0000000000000000").replace(
            "src/ota_boot_validation.cpp.o", "libFrameworkArduino.a(esp32-hal-misc.c.o)")
        self.check(mapping="Discarded input sections\n" + discarded + MAP)

    def test_rejects_framework_signature_changes(self):
        for old, new in (("verifyRollbackLater", "renamedHook"), ("bool", "int"),
                         ("()", "(int arg)"), ("weak", "unused")):
            with self.subTest(change=new), self.assertRaisesRegex(ValueError, "signature"):
                self.check(core=CORE.replace(old, new))

    def test_requires_actual_call_in_initialization(self):
        for replacement in ("if (true)", "// if (!verifyRollbackLater())\n",
                            "/* if (!verifyRollbackLater()) */"):
            with self.subTest(change=replacement), self.assertRaisesRegex(ValueError, "gates"):
                self.check(core=CORE.replace("if (!verifyRollbackLater())", replacement))

    def test_rejects_framework_owned_definition(self):
        with self.assertRaisesRegex(ValueError, "owned"):
            self.check(mapping=MAP.replace("src/ota_boot_validation.cpp.o",
                                          "libFrameworkArduino.a(esp32-hal-misc.c.o)"))

    def test_rejects_mangled_missing_or_duplicate_live_hook(self):
        for mapping in (MAP.replace("verifyRollbackLater", "_Z19verifyRollbackLaterv"),
                        "Linker script and memory map\n", MAP + SECTION,
                        SECTION + "Linker script and memory map\n"):
            with self.subTest(mapping=mapping), self.assertRaisesRegex(ValueError, "exactly one"):
                self.check(mapping=mapping)

    def test_rejects_zero_or_mismatched_address(self):
        for mapping in (MAP.replace("0x00000000401acb50", "0x0"),
                        MAP.replace("0x00000000401acb50", "0x401acb54", 1)):
            with self.subTest(mapping=mapping), self.assertRaisesRegex(ValueError, "owned"):
                self.check(mapping=mapping)

    def test_requires_framework_and_map_files(self):
        for core, mapping in ((None, MAP), (CORE, None)):
            with self.subTest(core=core), self.assertRaises(FileNotFoundError):
                self.check(core, mapping)

    def test_rejects_map_without_live_section(self):
        with self.assertRaisesRegex(ValueError, "live linker section"):
            self.check(mapping=SECTION)
