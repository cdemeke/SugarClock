import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PushHandlerTests(unittest.TestCase):
    def test_notify_and_sysmon_bodies_are_complete_and_bounded(self):
        library = ROOT / '.pio/libdeps/esp32dev/ArduinoJson/src'
        if not library.exists():
            self.skipTest('Install pinned firmware dependencies first')
        source = (ROOT / 'src/web_server.cpp').read_text()
        handlers = source[source.index('// POST /api/notify'):source.index('// POST /api/test/weather\n')]
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / 'push_handlers.inc').write_text(handlers)
            exe = str(pathlib.Path(tmp) / 'test_push_handlers')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-Iinclude', '-I' + tmp, '-I' + str(library), 'tests/test_push_handlers.cpp',
                            '-fsanitize=address,undefined', '-o', exe], cwd=ROOT, check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
