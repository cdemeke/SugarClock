import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TrendMappingTests(unittest.TestCase):
    def test_source_values_and_rendered_pixels(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp) / 'trends')
            subprocess.run([
                os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                '-I', str(ROOT / 'tests/glucose_display_stubs'),
                '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/test_trend_mapping.cpp'),
                str(ROOT / 'src/trend_mapping.cpp'),
                str(ROOT / 'src/libre_session.cpp'),
                str(ROOT / 'src/display.cpp'), '-o', binary,
            ], check=True)
            subprocess.run([binary], check=True)
