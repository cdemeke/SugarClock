import pathlib
import subprocess
import tempfile
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class BLEHostTests(unittest.TestCase):
    def test_actual_network_lease_in_both_profiles(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=(ROOT/'src/ble_manager.cpp').read_text()
            implementation=source[source.index('bool ble_acquire_network()'):source.index('void ble_loop()')]
            (pathlib.Path(tmp)/'ble_network.inc').write_text(implementation)
            for coexist in (0,1):
                exe=str(pathlib.Path(tmp)/('ble-network-'+str(coexist)))
                subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-variable',
                                '-DSUGARCLOCK_BLE_COEXIST_TEST='+str(coexist),'-Iinclude','-I'+tmp,
                                'tests/test_ble_network.cpp','-o',exe],cwd=ROOT,check=True)
                subprocess.run([exe],cwd=ROOT,check=True)

    def test_actual_connection_callbacks(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=(ROOT/'src/ble_manager.cpp').read_text()
            callbacks=source[source.index('bool windowOpen()'):source.index('class Characteristics:')]
            (pathlib.Path(tmp)/'ble_session.inc').write_text(callbacks)
            exe=str(pathlib.Path(tmp)/'ble-session')
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                            '-Iinclude','-I'+tmp,'tests/test_ble_session.cpp','-o',exe],cwd=ROOT,check=True)
            subprocess.run([exe],cwd=ROOT,check=True)

    def test_protocol_and_configuration(self):
        library=ROOT/'.pio/libdeps/esp32dev/ArduinoJson/src'
        if not library.exists():
            self.skipTest('Run pio pkg install or pio run first for pinned ArduinoJson headers')
        with tempfile.TemporaryDirectory() as tmp:
            exe=str(pathlib.Path(tmp)/'ble-tests')
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','-I'+str(library),'tests/test_ble_logic.cpp','src/config_patch.cpp','-o',exe],cwd=ROOT,check=True)
            subprocess.run([exe],cwd=ROOT,check=True)
