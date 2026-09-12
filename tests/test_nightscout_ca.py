"""Verify/regenerate the ESP32 compact Mozilla trust bundle without extra packages.

Source: curl.se's Mozilla root export, retained as a fixture with SHA-256.
Format matches ESP32 Arduino 2.0.17 WiFiClientSecure/src/esp_crt_bundle.c:
2-byte count, then sorted (2-byte subject length, 2-byte SPKI length, DER data).
Regenerate after reviewing an updated Mozilla export with:
python3 tests/test_nightscout_ca.py --regenerate path/to/cacert.pem
Update the dated retained fixture and source comment when updating root policy.
"""
import base64
import hashlib
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests/fixtures/nightscout/ca-roots-2026-08-13.pem"
CLIENT = ROOT / "src/nightscout_client.cpp"
SOURCE_SHA256 = "f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9"


def tlv(data, pos):
    start = pos
    tag = data[pos]
    pos += 1
    length = data[pos]
    pos += 1
    if length & 128:
        count = length & 127
        if not count or count > 4:
            raise ValueError("Invalid DER length")
        length = int.from_bytes(data[pos:pos + count], "big")
        pos += count
    if pos + length > len(data):
        raise ValueError("Truncated DER")
    return tag, start, pos, pos + length


def compact_bundle(pem):
    certificates = []
    for encoded in re.findall(rb"-----BEGIN CERTIFICATE-----\s*(.*?)\s*-----END CERTIFICATE-----", pem, re.S):
        der = base64.b64decode(encoded)
        _, _, body, _ = tlv(der, 0)
        _, _, tbs, end = tlv(der, body)
        fields = []
        while tbs < end:
            tag, start, _, tbs = tlv(der, tbs)
            fields.append((tag, der[start:tbs]))
        version_offset = 1 if fields[0][0] == 160 else 0
        subject = fields[version_offset + 4][1]
        spki = fields[version_offset + 5][1]
        certificates.append((subject, spki))
    certificates.sort(key=lambda pair: pair[0])
    bundle = len(certificates).to_bytes(2, "big")
    for subject, spki in certificates:
        bundle += len(subject).to_bytes(2, "big") + len(spki).to_bytes(2, "big") + subject + spki
    return bundle


class NightscoutTrustTests(unittest.TestCase):
    def test_embedded_roots_match_pinned_mozilla_export(self):
        pem = SOURCE.read_bytes()
        self.assertEqual(hashlib.sha256(pem).hexdigest(), SOURCE_SHA256)
        expected = compact_bundle(pem)
        source = CLIENT.read_text()
        array = re.search(r"NIGHTSCOUT_CA_BUNDLE\[\] = \{(.*?)\n\};", source, re.S).group(1)
        actual = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", array))
        self.assertEqual(actual, expected)
        self.assertEqual(int.from_bytes(actual[:2], "big"), 121)
        self.assertIn(SOURCE_SHA256, source)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--regenerate":
        pem = pathlib.Path(sys.argv[2]).read_bytes()
        bundle = compact_bundle(pem)
        lines = ["    " + ",".join("0x%02x" % value for value in bundle[i:i + 16]) + ","
                 for i in range(0, len(bundle), 16)]
        source = CLIENT.read_text()
        source = re.sub(r"(NIGHTSCOUT_CA_BUNDLE\[\] = \{).*?(\n\};)",
                        lambda match: match.group(1) + "\n" + "\n".join(lines) + match.group(2), source, flags=re.S)
        source = re.sub(r"Source PEM SHA-256: [0-9a-f]+", "Source PEM SHA-256: " + hashlib.sha256(pem).hexdigest(), source)
        CLIENT.write_text(source)
        print("Updated compact bundle; review/update dated source fixture, root count, and source policy comments.")
    else:
        unittest.main()
