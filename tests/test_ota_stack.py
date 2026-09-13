import importlib.util
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "check_ota_stack", Path(__file__).resolve().parents[1] / "scripts/check_ota_stack.py")
stack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stack)


class OtaStackBudgetTests(unittest.TestCase):
    def check_report(self, content):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "ota_manager.cpp.su"
            report.write_text(content)
            return stack.check_ota_stack(report)

    def test_accepts_bounded_worker(self):
        self.assertEqual(self.check_report(
            "ota_manager.cpp:1:1:void ota_worker(void*)\t1248\tstatic\n"), 1248)

    def test_rejects_original_overflowing_frame(self):
        with self.assertRaisesRegex(ValueError, "6224 bytes"):
            self.check_report("ota_manager.cpp:1:1:void ota_worker(void*)\t6224\tstatic\n")

    def test_rejects_unbounded_dynamic_stack(self):
        with self.assertRaisesRegex(ValueError, "unbounded"):
            self.check_report("ota_manager.cpp:1:1:void ota_worker(void*)\t64\tdynamic\n")

    def test_requires_worker_in_report(self):
        with self.assertRaisesRegex(ValueError, "missing"):
            self.check_report("ota_manager.cpp:1:1:void unrelated()\t64\tstatic\n")
