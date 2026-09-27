import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import ota_manifest as ota
import release_guard as guard


class LatestGuardTests(unittest.TestCase):
    def test_served_manifest_matches_immutable_tag(self):
        raw = b'{"version":"0.2.7"}'
        with mock.patch.object(guard.subprocess, 'check_output', return_value='{"tag_name":"v0.2.7"}'), \
             mock.patch.object(guard.urllib.request, 'urlopen', side_effect=[io.BytesIO(raw), io.BytesIO(raw)]):
            self.assertEqual(guard.latest_snapshot('owner/repo'), {
                'tag': 'v0.2.7', 'version': '0.2.7', 'manifest_sha256': hashlib.sha256(raw).hexdigest()})

    def test_metadata_and_legacy_url_disagreement_fails(self):
        with mock.patch.object(guard.subprocess, 'check_output', return_value='{"tag_name":"v0.2.7"}'), \
             mock.patch.object(guard.urllib.request, 'urlopen', side_effect=[io.BytesIO(b'old'), io.BytesIO(b'new')]):
            with self.assertRaisesRegex(ValueError, 'differs'):
                guard.latest_snapshot('owner/repo')

    def test_unchanged_offer_passes_and_mutated_manifest_or_tag_fails(self):
        initial = {'tag': 'v0.2.7', 'manifest_sha256': 'first', 'version': '0.2.7'}
        guard.assert_unchanged(initial, dict(initial))
        for change in ({'tag': 'v0.2.8'}, {'manifest_sha256': 'replacement'}):
            with self.assertRaisesRegex(ValueError, 'Latest changed'):
                guard.assert_unchanged(initial, {**initial, **change})


class BridgeGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.private = str(self.path / 'private.pem')
        self.public = str(self.path / 'public.pem')
        subprocess.run(['openssl', 'genrsa', '-out', self.private, '2048'], check=True, capture_output=True)
        subprocess.run(['openssl', 'rsa', '-in', self.private, '-pubout', '-out', self.public], check=True, capture_output=True)
        self.firmware = b'exact physical-tested bytes'
        (self.path / 'sugarclock-v0.2.8.bin').write_bytes(self.firmware)
        self.manifest = dict(schema=1, product=ota.PRODUCT, hardware=ota.HARDWARE,
                             channel='stable', version='0.2.8', minimum_ota_version='0.2.0',
                             size=len(self.firmware), sha256=hashlib.sha256(self.firmware).hexdigest(),
                             firmware_url='https://github.com/owner/repo/releases/download/v0.2.8/sugarclock-v0.2.8.bin',
                             key_id='release-2026-01', published_at='2026-09-12T00:00:00Z')

    def verify(self, previous='0.2.7'):
        self.manifest['signature'] = ota.sign_payload(ota.canonical_payload(self.manifest), self.private)
        (self.path / 'ota-manifest.json').write_text(json.dumps(self.manifest))
        return guard.verify_bridge(self.path, 'v0.2.8', 'owner/repo', self.public, previous)

    def test_exact_signed_artifacts_pass(self):
        self.assertEqual(self.verify()['tag'], 'v0.2.8')

    def test_modified_firmware_fails(self):
        (self.path / 'sugarclock-v0.2.8.bin').write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'bytes do not match'):
            self.verify()

    def test_preview_and_incompatible_legacy_minimum_fail(self):
        self.manifest['channel'] = 'preview'
        with self.assertRaisesRegex(ValueError, 'wrong_channel'):
            self.verify()
        self.manifest.update(channel='stable', minimum_ota_version='0.2.5')
        with self.assertRaisesRegex(ValueError, 'minimum_version_not_met'):
            self.verify()

    def test_equal_older_and_mutable_urls_fail(self):
        with self.assertRaisesRegex(ValueError, 'not_newer'):
            self.verify(previous='0.2.8')
        self.manifest['firmware_url'] = 'https://github.com/owner/repo/releases/latest/download/firmware.bin'
        with self.assertRaisesRegex(ValueError, 'immutable'):
            self.verify()
