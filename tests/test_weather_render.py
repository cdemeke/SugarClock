import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class WeatherDisplayTests(unittest.TestCase):
    def test_actual_renderer(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / "weather-display")
            subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "include"),
                str(ROOT / "tests/test_weather_render.cpp"),
                str(ROOT / "src/weather_render.cpp"), "-o", exe,
            ], check=True)
            subprocess.run([exe], check=True)


if __name__ == "__main__":
    unittest.main()
