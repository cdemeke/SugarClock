import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import ota_manifest as ota
import generate_web_assets as web_assets


class WebAssetTests(unittest.TestCase):
    def test_gzip_header_is_cross_platform_deterministic(self):
        payload = b"SugarClock embedded web asset"
        compressed = web_assets.deterministic_gzip(payload)
        self.assertEqual(compressed[9], 255)
        self.assertEqual(web_assets.gzip.decompress(compressed), payload)


class OtaManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.private = os.path.join(cls.temp.name, "private.pem")
        cls.public = os.path.join(cls.temp.name, "public.pem")
        cls.wrong_private = os.path.join(cls.temp.name, "wrong-private.pem")
        cls.wrong_public = os.path.join(cls.temp.name, "wrong-public.pem")
        subprocess.run(["openssl", "genrsa", "-out", cls.private, "2048"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["openssl", "rsa", "-in", cls.private, "-pubout", "-out", cls.public],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["openssl", "genrsa", "-out", cls.wrong_private, "2048"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["openssl", "rsa", "-in", cls.wrong_private, "-pubout", "-out", cls.wrong_public],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def manifest(self):
        firmware = b"esp32-firmware-image"
        manifest = {
            "schema": 1,
            "product": ota.PRODUCT,
            "hardware": ota.HARDWARE,
            "channel": ota.CHANNEL,
            "version": "0.2.1",
            "minimum_ota_version": "0.2.0",
            "size": len(firmware),
            "sha256": hashlib.sha256(firmware).hexdigest(),
            "firmware_url": "https://github.com/cdemeke/SugarClock/releases/download/v0.2.1/sugarclock-v0.2.1.bin",
            "key_id": "release-2026-01",
            "signature": "",
            "published_at": "2026-08-23T12:00:00Z",
        }
        manifest["signature"] = ota.sign_payload(ota.canonical_payload(manifest), self.private)
        return manifest

    def assert_error(self, expected, manifest, **kwargs):
        with self.assertRaisesRegex(ota.ManifestError, expected):
            ota.validate_manifest(manifest, **kwargs)

    def test_canonical_payload_has_exact_order_and_final_newline(self):
        payload = ota.canonical_payload(self.manifest()).decode()
        self.assertTrue(payload.startswith("sugarclock-ota-v1\nproduct=sugarclock\n"))
        self.assertTrue(payload.endswith("key_id=release-2026-01\n"))

    def test_valid_signature(self):
        manifest = self.manifest()
        self.assertTrue(ota.validate_manifest(manifest, current_version="0.2.0", public_key=self.public))

    def test_invalid_signature(self):
        manifest = self.manifest()
        # Always alter the encoded signature. A randomly generated RSA
        # signature can already begin with "A", which made this test flaky.
        replacement = "A" if manifest["signature"][0] != "A" else "B"
        manifest["signature"] = replacement + manifest["signature"][1:]
        self.assert_error("invalid_signature", manifest, public_key=self.public)

    def test_wrong_key(self):
        self.assert_error("invalid_signature", self.manifest(), public_key=self.wrong_public)

    def test_changed_signed_field(self):
        manifest = self.manifest()
        manifest["firmware_url"] += "?changed=1"
        self.assert_error("invalid_signature", manifest, public_key=self.public)

    def test_sha_mismatch(self):
        manifest = self.manifest()
        self.assert_error("sha256_mismatch", manifest, expected_sha256="0" * 64)

    def test_identity_rejections(self):
        for field, value, error in (
            ("product", "other", "wrong_product"),
            ("hardware", "other-board", "wrong_hardware"),
            ("channel", "beta", "wrong_channel"),
        ):
            manifest = self.manifest(); manifest[field] = value
            self.assert_error(error, manifest)

    def test_oversized_firmware(self):
        manifest = self.manifest(); manifest["size"] = 0x1C0001
        self.assert_error("firmware_too_large", manifest)

    def test_downgrade_and_equal_rejected(self):
        for version in ("0.1.9", "0.2.0"):
            manifest = self.manifest(); manifest["version"] = version
            self.assert_error("not_newer", manifest, current_version="0.2.0")

    def test_minimum_version_rejected(self):
        manifest = self.manifest(); manifest["minimum_ota_version"] = "0.3.0"
        self.assert_error("minimum_version_not_met", manifest, current_version="0.2.0")

    def test_manifest_size_limit_and_invalid_json(self):
        path = os.path.join(self.temp.name, "manifest.json")
        with open(path, "wb") as stream: stream.write(b"{" + b" " * ota.MAX_MANIFEST_BYTES)
        with self.assertRaisesRegex(ota.ManifestError, "manifest_too_large"):
            ota.load_manifest(path)
        with open(path, "wb") as stream: stream.write(b"not-json")
        with self.assertRaisesRegex(ota.ManifestError, "invalid_json"):
            ota.load_manifest(path)


class HostCppLogicTests(unittest.TestCase):
    def test_host_cpp_logic(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = os.path.join(temp, "host-tests")
            compiler = os.environ.get("CXX", "c++")
            subprocess.run([
                compiler, "-std=c++11", "-I", os.path.join(ROOT, "include"),
                os.path.join(ROOT, "tests", "test_host_logic.cpp"),
                os.path.join(ROOT, "src", "semver.cpp"),
                os.path.join(ROOT, "src", "ota_policy.cpp"), "-o", binary,
            ], check=True)
            subprocess.run([binary], check=True)


if __name__ == "__main__":
    unittest.main()
