import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NetcheckTests(unittest.TestCase):
    def test_production_reachability_recovery(self):
        source = (ROOT / "src/net_check.cpp").read_text()
        blocks = [source[source.index("enum Step {"):source.index("// Pull the bare hostname")],
                  source[source.index("static void build_summary()"):source.index("static bool probe_dns()")],
                  source[source.index("void netcheck_init()"):]]
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / "netcheck.inc").write_text("\n".join(blocks))
            exe = str(pathlib.Path(tmp) / "netcheck")
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-Iinclude", "-I" + tmp, "tests/test_netcheck.cpp", "-o", exe],
                           cwd=ROOT, check=True)
            subprocess.run([exe], check=True)
