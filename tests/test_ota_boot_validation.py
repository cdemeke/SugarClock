from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class OtaBootValidationLinkTests(unittest.TestCase):
    def test_validation_timing_and_failed_attempt_backoff(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "boot-policy"
            subprocess.run([
                "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "include"),
                str(ROOT / "tests/test_ota_boot_policy.cpp"),
                str(ROOT / "src/ota_boot_validation.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

    def test_application_overrides_framework_weak_c_hook(self):
        # Exercise the actual C/C++ link boundary. A C++-mangled hook silently
        # leaves Arduino's default early acceptance in effect on hardware.
        cc = shutil.which("cc")
        cxx = shutil.which("c++")
        self.assertIsNotNone(cc, "OTA linkage regression requires a C compiler")
        self.assertIsNotNone(cxx, "OTA linkage regression requires a C++ compiler")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            framework = root / "framework.c"
            framework.write_text("""
#include <stdbool.h>
bool verifyRollbackLater(void) __attribute__((weak));
bool verifyRollbackLater(void) { return false; }
int main(void) { return verifyRollbackLater() ? 0 : 1; }
""")
            obj = root / "framework.o"
            binary = root / "validation-hook"
            subprocess.run([cc, "-Wall", "-Wextra", "-Werror", "-c",
                            str(framework), "-o", str(obj)], check=True)
            subprocess.run([cxx, str(obj), "-o", str(binary)], check=True)
            self.assertEqual(subprocess.run([str(binary)]).returncode, 1)
            subprocess.run([cxx, "-Wall", "-Wextra", "-Werror", str(obj),
                            "-I", str(ROOT / "include"),
                            str(ROOT / "src/ota_boot_validation.cpp"),
                            "-o", str(binary)], check=True)
            self.assertEqual(subprocess.run([str(binary)]).returncode, 0)
