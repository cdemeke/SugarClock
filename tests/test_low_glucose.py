"""Run the production display engine against controlled readings and peripherals."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LowGlucoseTests(unittest.TestCase):
    def test_display_priority_navigation_recovery_and_freshness(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'low-glucose'
            subprocess.run([
                os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                '-Werror', '-Wno-error=unused-variable',
                '-I', str(ROOT / 'tests/engine_stubs'), '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/test_low_glucose.cpp'),
                str(ROOT / 'src/weather_render.cpp'), '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)
