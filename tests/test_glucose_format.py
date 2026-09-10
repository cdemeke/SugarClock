import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class GlucoseDisplayTests(unittest.TestCase):
    def test_unit_conversion_and_actual_renderer(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = (ROOT / "src/glucose_engine.cpp").read_text()
            start = source.index("        case STATE_STALE_WARNING: {", source.index("static void render_state("))
            end = source.index("        case STATE_NO_DATA:", start)
            (pathlib.Path(tmp) / "stale_screen.inc").write_text(source[start:end])
            exe = str(pathlib.Path(tmp) / "glucose-display")
            subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                # GCC flags the existing TREND_NAMES table in trend_arrows.h;
                # display.cpp uses its bitmaps but not its names. Keep this
                # warning visible without failing the renderer regression test.
                "-Wno-error=unused-variable",
                "-I", str(ROOT / "tests/glucose_display_stubs"), "-I", str(ROOT / "include"),
                "-I", tmp, str(ROOT / "tests/test_glucose_format.cpp"),
                str(ROOT / "src/display.cpp"), "-o", exe,
            ], check=True)
            subprocess.run([exe], check=True)
