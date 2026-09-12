"""Compile and execute production Nightscout logic against the pinned ArduinoJson."""
import os
import pathlib
import platform
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def arduinojson_include():
    explicit = os.environ.get("ARDUINOJSON_INCLUDE")
    candidates = ([pathlib.Path(explicit)] if explicit else []) + [
        ROOT / ".pio/libdeps/esp32dev/ArduinoJson/src",
    ]
    for candidate in candidates:
        if (candidate / "ArduinoJson.h").exists():
            return candidate
    raise RuntimeError("Install the pinned PlatformIO dependencies, or set ARDUINOJSON_INCLUDE")


class NightscoutTests(unittest.TestCase):
    def test_production_parser_and_request(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / "nightscout")
            command = [os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                       "-Wno-error=unused-variable", "-Wno-deprecated-declarations",
                       "-I", str(ROOT / "include"), "-I", str(arduinojson_include()),
                       str(ROOT / "tests/test_nightscout.cpp"), str(ROOT / "src/nightscout_logic.cpp"), "-o", exe]
            if platform.system() != "Darwin":
                command += ["-lcrypto"]
            subprocess.run(command, check=True)
            subprocess.run([exe], check=True)

    def test_production_transport(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / "nightscout-transport")
            command = [os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                       "-Wno-error=unused-variable", "-Wno-deprecated-declarations",
                       "-I", str(ROOT / "tests/test_nightscout_stubs"), "-I", str(ROOT / "include"),
                       "-I", str(arduinojson_include()), str(ROOT / "tests/test_nightscout_transport.cpp"),
                       str(ROOT / "src/nightscout_client.cpp"), str(ROOT / "src/nightscout_logic.cpp"), "-o", exe]
            if platform.system() != "Darwin":
                command += ["-lcrypto"]
            subprocess.run(command, check=True)
            subprocess.run([exe], check=True)

    def test_production_settings(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / "nightscout-settings")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-error=unused-variable", "-I", str(ROOT / "include"), "-I", str(arduinojson_include()),
                            str(ROOT / "tests/test_nightscout_settings.cpp"), str(ROOT / "src/nightscout_logic.cpp"), "-o", exe], check=True)
            subprocess.run([exe], check=True)
