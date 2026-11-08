"""Compile production persistence, publication and weather scheduling boundaries."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReviewFixTests(unittest.TestCase):
    def run_cpp(self, name, extracts):
        with tempfile.TemporaryDirectory() as tmp:
            for filename, text in extracts.items():
                (Path(tmp) / filename).write_text(text)
            exe = str(Path(tmp) / name)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-Iinclude', '-I' + tmp, 'tests/' + name + '.cpp', '-o', exe],
                           cwd=ROOT, check=True)
            subprocess.run([exe], check=True)

    def test_rejected_save_uses_latest_recoverable_journal(self):
        source = (ROOT / 'src/config_manager.cpp').read_text()
        self.run_cpp('test_config_recovery', {
            'config_defaults.inc': source[source.index('static void config_set_defaults()'):source.index('static void config_check_littlefs_overlay()')],
            'config_cycle_load.inc': '\n'.join(line for line in source.splitlines() if 'config.night_disable_auto_cycle = prefs.getBool' in line),
            'config_save.inc': source[
            source.index('bool config_save()'):source.index('bool config_bond_reset_pending()')]})

    def test_weather_worker_keeps_main_loop_free_and_serializes_requests(self):
        source = (ROOT / 'src/weather_client.cpp').read_text()
        self.run_cpp('test_weather_worker', {'weather_worker.inc':
            source[source.index('static WeatherSnapshot published;'):source.index('// Detect whether')] +
            source[source.index('static void weather_worker('):source.index('void weather_set_pre_fetch_callback(')]})

    def test_source_change_invalidates_publication_while_worker_is_blocked(self):
        source = (ROOT / 'src/http_client.cpp').read_text()
        self.run_cpp('test_http_publication', {'http_publication.inc':
            source[source.index('struct PublishedHTTP {'):source.index('unsigned long http_fetch_generation()')] +
            source[source.index('static void publish_result()'):source.index('static void fetch_worker(')] +
            source[source.index('void http_configuration_changed()'):source.index('GlucoseReading http_get_reading()')]})
