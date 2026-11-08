import concurrent.futures
import importlib.util
import pathlib
import socket
import ssl
import subprocess
import tarfile
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_tls", ROOT / "scripts/build_tls.py")
tls = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tls)


class TLSSourceIntegrityTests(unittest.TestCase):
    def test_rejects_corrupted_cached_source(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = pathlib.Path(directory)
            (cache / "mbedtls.tar.gz").write_bytes(b"changed source")
            with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
                tls.checked_archive(cache, "mbedtls")


class TLSTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cache = ROOT / ".pio/tls-source" / tls.MBEDTLS_COMMIT
        if not (cache / "mbedtls.tar.gz").exists():
            raise unittest.SkipTest("Run pio run first to cache the pinned TLS sources")
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = pathlib.Path(cls.temp.name)
        source = cls.directory / "source"
        prefix = "mbedtls-" + tls.MBEDTLS_COMMIT + "/"
        with tarfile.open(tls.checked_archive(cache, "mbedtls")) as archive:
            for member in archive.getmembers():
                if not member.name.startswith(prefix):
                    continue
                relative = pathlib.PurePosixPath(member.name[len(prefix):])
                if ".." in relative.parts or not member.isfile() or relative.parts[0] not in ("include", "library"):
                    continue
                tls.write_if_changed(source / relative, tls.member_bytes(archive, member.name))
        flags = ["cc", "-O1", "-std=c99", "-Wno-deprecated-declarations", "-I" + str(source / "include"),
                 "-DMBEDTLS_SSL_IN_CONTENT_LEN=16384", "-DMBEDTLS_SSL_OUT_CONTENT_LEN=4096"]
        files = sorted((source / "library").glob("*.c")) + [ROOT / "tests/test_tls_transport.c"]
        def compile_file(path):
            obj = cls.directory / (path.name + ".o")
            subprocess.run(flags + ["-c", str(path), "-o", str(obj)], check=True, capture_output=True)
            return str(obj)
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            objects = list(pool.map(compile_file, files))
        cls.client = cls.directory / "client"
        subprocess.run(["cc", *objects, "-o", str(cls.client)], check=True, capture_output=True)
        cls.cert = cls.directory / "cert.pem"
        cls.key = cls.directory / "key.pem"
        conf = cls.directory / "cert.conf"
        conf.write_text("[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n"
                        "[dn]\nCN=localhost\n[ext]\nsubjectAltName=DNS:localhost\n"
                        "basicConstraints=critical,CA:TRUE\n")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-config", str(conf), "-keyout", str(cls.key), "-out", str(cls.cert)],
                       check=True, capture_output=True)

    def exchange(self, hostname):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(10)
        errors = []
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(self.cert, self.key)
        def serve():
            try:
                with listener:
                    raw, _ = listener.accept()
                    raw.settimeout(10)
                    with context.wrap_socket(raw, server_side=True) as conn:
                        request = bytearray()
                        while len(request) < 20000:
                            chunk = conn.recv(20000 - len(request))
                            if not chunk:
                                raise AssertionError("Truncated outgoing data")
                            request.extend(chunk)
                        if request != bytes(i % 251 for i in range(20000)):
                            raise AssertionError("Outgoing payload changed")
                        conn.sendall(bytes((i * 7) % 251 for i in range(32768)))
            except Exception as error:
                errors.append(error)
        port = listener.getsockname()[1]
        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        result = subprocess.run([str(self.client), str(port), str(self.cert), hostname],
                                capture_output=True, text=True, timeout=12)
        worker.join(12)
        self.assertFalse(worker.is_alive(), "TLS test server did not finish")
        return result, errors

    def test_verified_encryption_and_large_bidirectional_payloads(self):
        result, errors = self.exchange("localhost")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(errors, [])
        self.assertIn("certificate verified", result.stdout)

    def test_wrong_server_identity_is_rejected(self):
        result, _ = self.exchange("wrong-host.invalid")
        self.assertEqual(result.returncode, 3, result.stderr)


if __name__ == "__main__":
    unittest.main()
