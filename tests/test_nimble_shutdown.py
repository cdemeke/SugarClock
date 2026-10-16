"""Exercise the actual pinned NimBLE shutdown code, not a rewritten equivalent."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("patch_nimble", ROOT / "scripts/patch_nimble.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def function(source, name, declaration):
    start = source.index(declaration + "\n" + name + "(")
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class NimBLEShutdownTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = ROOT / ".pio/libdeps/esp32dev/NimBLE-Arduino" / patch.SOURCE
        if not source.exists():
            raise unittest.SkipTest("Run pio pkg install -e esp32dev for the pinned NimBLE sources")
        data = source.read_bytes()
        cls.patched = patch.patched_source(data)
        # Reconstruct the verified pristine source if a firmware build patched it.
        cls.original = cls.patched.replace(patch.AFTER, patch.BEFORE, 1)
        assert hashlib.sha256(cls.original).hexdigest() == patch.ORIGINAL_SHA256

    def library(self, root, source=None, version="2.5.0"):
        (root / "library.properties").write_text("name=NimBLE-Arduino\nversion=" + version + "\n")
        target = root / patch.SOURCE
        target.parent.mkdir(parents=True)
        target.write_bytes(self.original if source is None else source)
        return target

    def test_exact_upstream_patch_and_repeat_build_noop(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = self.library(root)
            self.assertTrue(patch.patch_library(root))
            self.assertEqual(target.read_bytes(), self.patched)
            modified = target.stat().st_mtime_ns
            self.assertFalse(patch.patch_library(root))
            self.assertEqual(target.stat().st_mtime_ns, modified)
            self.assertEqual(self.original.count(b"ble_npl_callout_deinit(&ble_hs_timer);"), 2)
            self.assertEqual(self.patched.count(b"ble_npl_callout_deinit(&ble_hs_timer);"), 1)

    def test_changed_pristine_or_patched_source_fails_closed(self):
        for data in (self.original, self.patched):
            with self.subTest(patched=data == self.patched), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                altered = data + b"\n/* unreviewed change */\n"
                target = self.library(root, altered)
                with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
                    patch.patch_library(root)
                self.assertEqual(target.read_bytes(), altered)

    def test_dependency_upgrade_requires_review(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = self.library(root, version="2.5.1")
            with self.assertRaisesRegex(RuntimeError, "requires pinned"):
                patch.patch_library(root)
            self.assertEqual(target.read_bytes(), self.original)

    def test_interrupted_write_preserves_pristine_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = self.library(root)
            with mock.patch.object(patch.os, "fsync", side_effect=OSError("interrupted")):
                with self.assertRaisesRegex(OSError, "interrupted"):
                    patch.patch_library(root)
            self.assertEqual(target.read_bytes(), self.original)
            self.assertEqual(list(target.parent.glob(".ble_hs-*")), [])

    def test_configuration_hook_handles_fresh_environment_and_repeat_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            library = root / "fresh-environment" / "NimBLE-Arduino"
            library.mkdir(parents=True)
            source = self.library(library)
            env = mock.Mock()
            env.subst.side_effect = {"$PROJECT_LIBDEPS_DIR": str(root), "$PIOENV": "fresh-environment"}.__getitem__
            patch.configure(env)
            self.assertEqual(source.read_bytes(), self.patched)
            patch.configure(env)
            self.assertEqual(source.read_bytes(), self.patched)

    def test_queued_timer_survives_stop_until_final_deinitialization(self):
        harness = (ROOT / "tests/test_nimble_shutdown.c").read_text()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, source, expected in (("original", self.original, 12), ("patched", self.patched, 0)):
                actual = source.decode()
                extracted = function(actual, "ble_hs_timer_reset", "static void") + "\n"
                extracted += function(actual, "ble_hs_deinit", "void")
                (root / "host.inc").write_text(extracted)
                (root / "test.c").write_text(harness)
                executable = root / name
                subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(root / "test.c"),
                                "-o", str(executable)], check=True, capture_output=True)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, name + ": " + result.stderr)


if __name__ == "__main__":
    unittest.main()
