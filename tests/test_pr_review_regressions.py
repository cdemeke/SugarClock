import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]

class ReviewRegressionTests(unittest.TestCase):
    def compile_run(self, tmp, test, extra=()):
        library=ROOT/'.pio/libdeps/esp32dev/ArduinoJson/src'
        if not library.exists():
            self.skipTest('Install pinned firmware dependencies first')
        exe=str(pathlib.Path(tmp)/test)
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','-I'+tmp,'-I'+str(library),'tests/'+test+'.cpp',*extra,'-o',exe],cwd=ROOT,check=True)
        subprocess.run([exe],check=True)

    def test_actual_settings_application_for_all_persistence_outcomes(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=(ROOT/'src/settings_apply.cpp').read_text()
            (pathlib.Path(tmp)/'settings_apply.inc').write_text(source[source.index('bool settings_apply('):])
            self.compile_run(tmp,'test_settings_apply',['src/config_patch.cpp'])

    def test_actual_wifi_scan_and_ble_failure_responses(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=(ROOT/'src/wifi_manager.cpp').read_text()
            start=source[source.index('bool wifi_scan_start()'):source.index('// Fold the raw scan')]
            poll=source[source.index('    // Async scan bookkeeping'):source.index('    // Deferred AP teardown')]
            (pathlib.Path(tmp)/'wifi_scan.inc').write_text(start+'\nvoid poll_scan() {\n'+poll+'}\n'+source[source.index('bool wifi_scan_failed()'):source.index('bool wifi_scan_in_progress()')])
            ble=(ROOT/'src/ble_manager.cpp').read_text()
            operations=ble[ble.index(' } else if(!strcmp(op,"wifi.scan"))'):ble.index(' } else if(!strcmp(op,"wifi.trial"))')]
            (pathlib.Path(tmp)/'ble_scan.inc').write_text('void execute_scan(const char* op,JsonDocument& out) {if(false) {\n'+operations+'}}\n')
            self.compile_run(tmp,'test_wifi_scan')

    def test_actual_web_body_handlers_bounds_isolation_and_erasure(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=(ROOT/'src/web_server.cpp').read_text()
            config=source[source.index('static void handle_post_config('):source.index('// GET /api/debug')]
            wifi=source[source.index('// POST /api/wifi/connect'):source.index('// DELETE /api/wifi/ca')]
            (pathlib.Path(tmp)/'web_bodies.inc').write_text(config+'\n'+wifi)
            self.compile_run(tmp,'test_web_bodies',['src/config_patch.cpp','-fsanitize=address,undefined','-fno-omit-frame-pointer'])
