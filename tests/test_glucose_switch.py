import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]

class GlucoseSwitchTests(unittest.TestCase):
    def test_production_display_and_alert_guards(self):
        source = (ROOT / 'src/glucose_engine.cpp').read_text()
        blocks = []
        for start, end in [('void engine_rebuild_toggle_order()', '// Get color for glucose'),
                           ('static void check_alerts()', '// Snooze alerts'),
                           ('static DisplayState evaluate_state()', 'static void render_state(')]:
            blocks.append(source[source.index(start):source.index(end)])
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / 'glucose_switch.inc').write_text('\n'.join(blocks))
            exe = str(pathlib.Path(tmp) / 'glucose-switch')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Iinclude', '-I'+tmp,
                            'tests/test_glucose_switch.cpp', '-o', exe], cwd=ROOT, check=True)
            subprocess.run([exe], check=True)

    def test_previous_and_current_configuration_journals(self):
        source = (ROOT / 'src/config_manager.cpp').read_text()
        block = source[source.index('    const size_t journal_size='):source.index('    committed=config;')]
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / 'glucose_journal.inc').write_text(block)
            exe = str(pathlib.Path(tmp) / 'glucose-journal')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+tmp,
                            'tests/test_glucose_journal.cpp', '-o', exe], cwd=ROOT, check=True)
            subprocess.run([exe], check=True)
