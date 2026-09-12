import os
import re
import ssl
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PEM_RE = r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----"


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as stream:
        return stream.read()


def certificate_fields(pem, *fields):
    with tempfile.NamedTemporaryFile("w", suffix=".pem", delete=False) as stream:
        stream.write(pem + "\n")
        path = stream.name
    try:
        return subprocess.run(
            ["openssl", "x509", "-in", path, "-noout", *fields],
            check=True, text=True, capture_output=True,
        ).stdout
    finally:
        os.unlink(path)


def handshake(client_context, server_context, hostname):
    """Exercise certificate verification without network access or credentials."""
    client_in, client_out = ssl.MemoryBIO(), ssl.MemoryBIO()
    server_in, server_out = ssl.MemoryBIO(), ssl.MemoryBIO()
    client = client_context.wrap_bio(client_in, client_out, server_hostname=hostname)
    server = server_context.wrap_bio(server_in, server_out, server_side=True)
    client_done = server_done = False
    for _ in range(20):
        if not client_done:
            try:
                client.do_handshake()
                client_done = True
            except ssl.SSLWantReadError:
                pass
        server_in.write(client_out.read())
        if not server_done:
            try:
                server.do_handshake()
                server_done = True
            except ssl.SSLWantReadError:
                pass
        client_in.write(server_out.read())
        if client_done and server_done:
            return
    raise AssertionError("TLS handshake did not complete")


class LibreTlsTests(unittest.TestCase):
    def test_untrusted_server_is_rejected_by_the_libre_bundle(self):
        with tempfile.TemporaryDirectory() as tmp:
            key = os.path.join(tmp, "key.pem")
            cert = os.path.join(tmp, "cert.pem")
            subprocess.run([
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-keyout", key, "-out", cert, "-days", "1",
                "-subj", "/CN=api.libreview.io",
            ], check=True, capture_output=True)
            server = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            server.load_cert_chain(cert, key)

            # Positive control: the same server succeeds when explicitly trusted.
            trusted = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            trusted.load_verify_locations(cafile=cert)
            handshake(trusted, server, "api.libreview.io")
            with self.assertRaises(ssl.SSLCertVerificationError):
                handshake(trusted, server, "api-us.libreview.io")

            bundle = "\n".join(re.findall(
                PEM_RE, read("include", "libre_trusted_roots.h"), flags=re.DOTALL,
            ))
            libre = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            libre.load_verify_locations(cadata=bundle)
            with self.assertRaises(ssl.SSLCertVerificationError):
                handshake(libre, server, "api.libreview.io")

    def test_bundle_covers_every_libre_region(self):
        bundle = re.findall(PEM_RE, read("include", "libre_trusted_roots.h"), flags=re.DOTALL)
        subjects = "".join(certificate_fields(pem, "-subject") for pem in bundle)

        # *.libreview.io (Cloudflare, currently GTS Root R4), api.libreview.ru,
        # and api-cn.myfreestyle.cn
        self.assertIn("GTS Root R4", subjects)
        self.assertIn("Go Daddy Root Certificate Authority - G2", subjects)
        self.assertIn("Amazon Root CA 1", subjects)

        # Cloudflare may reissue from any of its CAs, so keep its full root set
        cloudflare = re.findall(PEM_RE, read("include", "ota_trusted_roots.h"), flags=re.DOTALL)
        normalized = {re.sub(r"\s", "", pem) for pem in bundle}
        for pem in cloudflare:
            self.assertIn(re.sub(r"\s", "", pem), normalized)

    def test_bundle_roots_are_not_about_to_expire(self):
        bundle = re.findall(PEM_RE, read("include", "libre_trusted_roots.h"), flags=re.DOTALL)
        self.assertGreaterEqual(len(bundle), 7)
        for pem in bundle:
            # Fails if the root expires within ~3 years (94608000 seconds)
            result = subprocess.run(
                ["openssl", "x509", "-noout", "-checkend", "94608000"],
                input=pem + "\n", text=True, capture_output=True,
            )
            self.assertEqual(result.returncode, 0, certificate_fields(pem, "-subject", "-enddate"))

    def test_libre_client_verifies_certificates(self):
        source = read("src", "libre_client.cpp")
        code = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.DOTALL)
        self.assertNotIn("setInsecure", code)
        self.assertIn("setCACert(LIBRE_TRUSTED_ROOTS_PEM)", code)


if __name__ == "__main__":
    unittest.main()
