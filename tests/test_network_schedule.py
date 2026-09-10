import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NetworkScheduleTests(unittest.TestCase):
    def test_production_schedulers(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = (ROOT / "src/http_client.cpp").read_text()
            (pathlib.Path(tmp) / "http_loop.inc").write_text(
                source[source.index("void http_loop()"):source.index("GlucoseReading http_get_reading()")])
            source = (ROOT / "src/fleet_manager.cpp").read_text()
            (pathlib.Path(tmp) / "fleet_loop.inc").write_text(source[source.index("void fleet_loop()"):])
            exe = str(pathlib.Path(tmp) / "network-loops")
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-Iinclude", "-I" + tmp, "tests/test_network_loops.cpp", "src/fleet_policy.cpp", "-o", exe],
                           cwd=ROOT, check=True)
            subprocess.run([exe], check=True)

    def test_timestamp_alignment_retries_and_batching(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / "network-schedule")
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-include", "initializer_list", "-Iinclude",
                            "tests/test_network_schedule.cpp", "-o", exe], cwd=ROOT, check=True)
            subprocess.run([exe], check=True)
