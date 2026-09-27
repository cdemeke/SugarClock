import importlib.util
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "check_ota_stack", Path(__file__).resolve().parents[1] / "scripts/check_ota_stack.py")
stack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stack)


class OtaStackBudgetTests(unittest.TestCase):
    def check_reports(self, overrides=None):
        with tempfile.TemporaryDirectory() as directory:
            reports = {
                "ota_manager.cpp.su": "ota_manager.cpp:1:1:void ota_worker(void*)\t1248\tstatic\n",
                "ota_manifest.cpp.su": "ota_manifest.cpp:1:1:bool ota_manifest_verify_signature()\t1376\tstatic\n",
                "fleet_manager.cpp.su": "fleet_manager.cpp:1:1:bool fleet_authorize_update()\t336\tstatic\n",
            }
            reports.update(overrides or {})
            for filename, content in reports.items():
                if content is not None:
                    (Path(directory) / filename).write_text(content)
            return stack.check_ota_stack(directory)

    def test_accepts_all_ota_task_reports(self):
        self.assertEqual(self.check_reports(), {
            "ota_manager.cpp.su": 1248,
            "ota_manifest.cpp.su": 1376,
            "fleet_manager.cpp.su": 336,
        })

    def test_rejects_original_overflowing_frame(self):
        with self.assertRaisesRegex(ValueError, "6224 bytes"):
            self.check_reports({
                "ota_manager.cpp.su": "ota_manager.cpp:1:1:void ota_worker(void*)\t6224\tstatic\n"})

    def test_rejects_large_frames_in_other_ota_task_sources(self):
        for filename in ("ota_manifest.cpp.su", "fleet_manager.cpp.su"):
            with self.subTest(report=filename), self.assertRaisesRegex(ValueError, "2049 bytes"):
                self.check_reports({filename: "source.cpp:1:1:void helper()\t2049\tstatic\n"})

    def test_rejects_unbounded_dynamic_stack(self):
        with self.assertRaisesRegex(ValueError, "unbounded"):
            self.check_reports({
                "ota_manager.cpp.su": "ota_manager.cpp:1:1:void ota_worker(void*)\t64\tdynamic\n"})

    def test_accepts_bounded_dynamic_frame_at_limit(self):
        result = self.check_reports({
            "ota_manager.cpp.su": "ota_manager.cpp:1:1:void ota_worker(void*)\t2048\tdynamic,bounded\n"})
        self.assertEqual(result["ota_manager.cpp.su"], 2048)

    def test_checks_helpers_in_addition_to_required_functions(self):
        with self.assertRaisesRegex(ValueError, "2049 bytes"):
            self.check_reports({"fleet_manager.cpp.su": (
                "fleet_manager.cpp:1:1:bool fleet_authorize_update()\t336\tstatic\n"
                "fleet_manager.cpp:2:1:int post_json_to_base()\t2049\tstatic\n")})

    def test_requires_each_report(self):
        for filename in stack.REQUIRED_REPORTS:
            with self.subTest(report=filename), self.assertRaises(FileNotFoundError):
                self.check_reports({filename: None})

    def test_requires_expected_function_in_each_report(self):
        for filename in stack.REQUIRED_REPORTS:
            with self.subTest(report=filename), self.assertRaisesRegex(ValueError, "missing"):
                self.check_reports({filename: "source.cpp:1:1:void unrelated()\t64\tstatic\n"})

    def test_rejects_empty_reports(self):
        for filename in stack.REQUIRED_REPORTS:
            with self.subTest(report=filename), self.assertRaisesRegex(ValueError, "missing"):
                self.check_reports({filename: ""})
