import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class GlucoseDisplayTests(unittest.TestCase):
    def test_progressive_mapping_mutation_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'display.cpp'
            original = (ROOT / 'src/display.cpp').read_text()
            mapping = '((y & 1) ? MATRIX_WIDTH - 1 - x : x)'
            self.assertIn(mapping, original)
            source.write_text(original.replace(mapping, 'x'))
            exe = str(pathlib.Path(tmp) / 'wrong-wiring')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11',
                            '-I', str(ROOT / 'tests/glucose_display_stubs'), '-I', str(ROOT / 'include'),
                            str(ROOT / 'tests/test_glucose_format.cpp'), str(source),
                            str(ROOT / 'src/glucose_render.cpp'), '-o', exe], check=True)
            result = subprocess.run([exe], cwd=tmp, capture_output=True)
            self.assertNotEqual(result.returncode, 0, 'Progressive wiring must fail the logical-coordinate assertions')

    def test_unit_conversion_and_actual_renderer(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / "glucose-display")
            subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                # GCC flags the existing TREND_NAMES table in trend_arrows.h;
                # display.cpp uses its bitmaps but not its names. Keep this
                # warning visible without failing the renderer regression test.
                "-Wno-error=unused-variable",
                "-I", str(ROOT / "tests/glucose_display_stubs"), "-I", str(ROOT / "include"),
                str(ROOT / "tests/test_glucose_format.cpp"),
                str(ROOT / "src/display.cpp"), str(ROOT / "src/glucose_render.cpp"), "-o", exe,
            ], check=True)
            subprocess.run([exe], check=True)
