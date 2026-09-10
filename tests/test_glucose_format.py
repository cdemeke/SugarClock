import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class GlucoseDisplayTests(unittest.TestCase):
    def test_unit_conversion_and_actual_renderer(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / "glucose-display")
            subprocess.run([
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-Itests/display_stubs", "-Iinclude", "tests/test_glucose_format.cpp",
                "src/display.cpp", "-o", exe,
            ], cwd=ROOT, check=True)
            subprocess.run([exe], cwd=ROOT, check=True)
