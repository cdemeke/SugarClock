import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
class DisplayFrameTests(unittest.TestCase):
    def test_actual_display_frame_submission(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / 'display')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-Itests/display_stubs', '-Iinclude', 'tests/test_display.cpp',
                            'src/display.cpp', '-o', exe], cwd=ROOT, check=True)
            subprocess.run([exe], cwd=ROOT, check=True)

    def test_actual_ble_renderer_priority_and_stability(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = (ROOT / 'src/ble_manager.cpp').read_text()
            renderer = 'void ble_render() {' + source.split('void ble_render() {', 1)[1]
            (pathlib.Path(tmp) / 'ble_render.inc').write_text(renderer)
            exe = str(pathlib.Path(tmp) / 'ble-display')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-Itests/display_stubs', '-Iinclude', '-I' + tmp,
                            'tests/test_ble_display.cpp', 'src/display.cpp', '-o', exe],
                           cwd=ROOT, check=True)
            subprocess.run([exe], cwd=ROOT, check=True)
