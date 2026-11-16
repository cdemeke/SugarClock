import os
import re
import ssl
import subprocess
import tempfile
import unittest

from test_libre_tls import PEM_RE, ROOT, handshake, read

FINGERPRINTS = {
    "DEXCOM_TRUSTED_ROOTS_PEM": {
        "d947432abde7b7fa90fc2e6b59101b1280e0e1c7e4e40fa3c6887fff57a7f4cf",  # GTS Root R1
        "349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d",  # GTS Root R4
    },
    "WEATHER_TRUSTED_ROOTS_PEM": {
        "7bb647a62aeeac88bf257aa522d01ffea395e0ab45c73f93f65654ec38f25a06",  # Sectigo R46
        "c90f26f0fb1b4018b22227519b5ca2b53e2ca5b3be5cf18efe1bef47380c5383",  # Sectigo E46
        "e793c9b02fd8aa13e21c31228accb08119643b749c898964b1746d46c3d4cbd2",  # USERTrust RSA
        "4ff460d54b9c86dabfbcfc5712e0400d2bed3fbc4d4fbdaa86e06adcd2a9ad7a",  # USERTrust ECC
    },
    "CUSTOM_SERVER_TRUSTED_ROOTS_PEM": {
        "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6",  # ISRG Root X1
        "69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470",  # ISRG Root X2
        "d947432abde7b7fa90fc2e6b59101b1280e0e1c7e4e40fa3c6887fff57a7f4cf",  # GTS Root R1
        "349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d",  # GTS Root R4
        "8ecde6884f3d87b1125ba31ac3fcb13d7016de7f57cc904fe1cb97c6ae98196e",  # Amazon Root CA 1
        "cb3ccbb76031e5e0138f8dd39a23f9de47ffc35e43c1144cea27d46a5ab1cb5f",  # DigiCert G2
        "4348a0e9444c78cb265e058d5e8944b4d84f9662bd26db257f8934a443c70161",  # DigiCert Global Root CA
        "e793c9b02fd8aa13e21c31228accb08119643b749c898964b1746d46c3d4cbd2",  # USERTrust RSA
        "7bb647a62aeeac88bf257aa522d01ffea395e0ab45c73f93f65654ec38f25a06",  # Sectigo R46
    },
}


def bundle(name):
    source = read("include", "service_trusted_roots.h")
    body = source[source.index(name):].split(')CERT"')[0]
    return re.findall(PEM_RE, body, flags=re.DOTALL)


def fingerprint(pem):
    out = subprocess.run(["openssl", "x509", "-noout", "-fingerprint", "-sha256"],
                         input=pem + "\n", text=True, capture_output=True, check=True).stdout
    return out.split("=", 1)[1].strip().replace(":", "").lower()


def chains_to(name, fixture):
    with tempfile.NamedTemporaryFile("w", suffix=".pem", delete=False) as stream:
        stream.write("\n".join(bundle(name)) + "\n")
        path = stream.name
    try:
        return subprocess.run(
            # Only the bundle under test: never fall back to the system trust store.
            ["openssl", "verify", "-no_check_time", "-no-CApath", "-no-CAstore", "-CAfile", path,
             os.path.join(ROOT, "tests", "fixtures", "tls", fixture)],
            capture_output=True, text=True,
        ).returncode == 0
    finally:
        os.unlink(path)


class ServiceTlsTests(unittest.TestCase):
    def test_bundles_hold_exactly_the_reviewed_roots(self):
        for name, expected in FINGERPRINTS.items():
            with self.subTest(bundle=name):
                self.assertEqual({fingerprint(pem) for pem in bundle(name)}, expected)

    def test_live_service_intermediates_chain_to_their_bundle(self):
        # Captured from share2.dexcom.com and api.openweathermap.org.
        self.assertTrue(chains_to("DEXCOM_TRUSTED_ROOTS_PEM", "dexcom-share-intermediate.pem"))
        self.assertTrue(chains_to("WEATHER_TRUSTED_ROOTS_PEM", "openweather-intermediate.pem"))
        self.assertFalse(chains_to("DEXCOM_TRUSTED_ROOTS_PEM", "openweather-intermediate.pem"))

    def test_untrusted_server_is_rejected_by_every_bundle(self):
        with tempfile.TemporaryDirectory() as tmp:
            key, cert = os.path.join(tmp, "key.pem"), os.path.join(tmp, "cert.pem")
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                            "-keyout", key, "-out", cert, "-days", "1",
                            "-subj", "/CN=share2.dexcom.com"], check=True, capture_output=True)
            server = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            server.load_cert_chain(cert, key)
            for name in FINGERPRINTS:
                with self.subTest(bundle=name):
                    client = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
                    client.load_verify_locations(cadata="\n".join(bundle(name)))
                    with self.assertRaises(ssl.SSLCertVerificationError):
                        handshake(client, server, "share2.dexcom.com")

    def test_roots_are_not_about_to_expire(self):
        for name in FINGERPRINTS:
            for pem in bundle(name):
                result = subprocess.run(["openssl", "x509", "-noout", "-checkend", "94608000"],
                                        input=pem + "\n", text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, name)

    def test_glucose_and_weather_clients_verify_certificates(self):
        for path, names in (("http_client.cpp", ("DEXCOM_TRUSTED_ROOTS_PEM", "CUSTOM_SERVER_TRUSTED_ROOTS_PEM")),
                            ("weather_client.cpp", ("WEATHER_TRUSTED_ROOTS_PEM",))):
            code = re.sub(r"//[^\n]*|/\*.*?\*/", "", read("src", path), flags=re.DOTALL)
            self.assertNotIn("setInsecure", code, path)
            for name in names:
                self.assertIn(f"setCACert({name})", code, path)


if __name__ == "__main__":
    unittest.main()
