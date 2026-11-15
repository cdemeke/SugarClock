"""Glucose alarm patterns, selection and buzzer priority, compiled on the host."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CXX = os.environ.get('CXX', 'c++')
FLAGS = ['-std=c++17', '-Wall', '-Wextra', '-Werror']


class AlarmTests(unittest.TestCase):
    def test_patterns(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / 'alarm-patterns')
            subprocess.run([CXX, *FLAGS, '-Iinclude', 'tests/test_alarm_patterns.cpp',
                            'src/alarm_pattern.cpp', '-o', exe], cwd=ROOT, check=True)
            subprocess.run([exe], check=True)

    def test_selection_snooze_and_buzzer_priority(self):
        source = (ROOT / 'src/glucose_engine.cpp').read_text()
        start, end = 'static bool alert_snooze_active()', 'void engine_init()'
        self.assertEqual(source.count(start), 1)
        block = source[source.index(start):source.index(end)]
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / 'alarm_selection.inc').write_text(block)
            exe = str(pathlib.Path(tmp) / 'alarm-selection')
            subprocess.run([CXX, *FLAGS, '-Itests/engine_stubs', '-Iinclude', '-I' + tmp,
                            'tests/test_alarm_selection.cpp', 'src/alarm_pattern.cpp', '-o', exe],
                           cwd=ROOT, check=True)
            subprocess.run([exe], check=True)

    def test_sound_test_routes_follow_factory_reset(self):
        source = (ROOT / 'src/web_server.cpp').read_text()
        reset = source.index('static void handle_factory_reset(')
        for name in ('handle_test_alarm', 'handle_test_tone', 'handle_test_sweep',
                     'handle_test_sound_stop'):
            self.assertGreater(source.index('static void %s(' % name), reset)
        for route in ('/api/test/alarm', '/api/test/tone', '/api/test/sweep',
                      '/api/test/sound/stop'):
            self.assertIn('server.on("%s", HTTP_POST' % route, source)


if __name__ == '__main__':
    unittest.main()
