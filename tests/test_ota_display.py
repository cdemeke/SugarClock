import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OtaDisplayTests(unittest.TestCase):
    def test_lifecycle_and_actual_engine_renderer(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = str(Path(temp) / "ota-display")
            subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra",
                "-I", str(ROOT / "tests/glucose_display_stubs"),
                "-I", str(ROOT / "include"),
                str(ROOT / "tests/test_ota_display.cpp"),
                str(ROOT / "src/display.cpp"), str(ROOT / "src/glucose_render.cpp"),
                "-o", binary,
            ], check=True)
            subprocess.run([binary], check=True, cwd=temp)
