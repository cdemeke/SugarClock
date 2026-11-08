import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ServerTokenTests(unittest.TestCase):
    def test_token_is_cleared_when_server_origin_changes(self):
        library = ROOT / '.pio/libdeps/esp32dev/ArduinoJson/src'
        if not library.exists():
            self.skipTest('Install pinned firmware dependencies first')
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / 'test_server_token')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Iinclude', '-I' + str(library),
                            'tests/test_server_token.cpp', 'src/config_patch.cpp', 'src/libre_patient.cpp',
                            '-fsanitize=address,undefined', '-o', exe], cwd=ROOT, check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
