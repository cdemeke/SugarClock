import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HttpNetworkTests(unittest.TestCase):
    def test_nightscout_publication_and_forced_request_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable = str(Path(tmp) / "http-network")
            subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-pthread", "-I", str(ROOT / "tests/http_network_stubs"),
                "-I", str(ROOT / "include"), str(ROOT / "tests/test_http_network.cpp"),
                "-o", executable,
            ], check=True)
            subprocess.run([executable], check=True, timeout=10)
