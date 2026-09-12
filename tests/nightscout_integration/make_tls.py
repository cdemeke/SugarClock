#!/usr/bin/env python3
"""Generate a two-day localhost-only test certificate outside tracked sources."""
from pathlib import Path
import subprocess
import sys


def generate(directory):
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    config = directory / 'tls.cnf'
    config.write_text('''[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = localhost
[ext]
subjectAltName = DNS:localhost,IP:127.0.0.1
basicConstraints = critical,CA:TRUE
keyUsage = critical,digitalSignature,keyEncipherment,keyCertSign
''')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
                    '-config', str(config), '-keyout', str(directory / 'test.key'),
                    '-out', str(directory / 'test.crt')], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # The non-root Nightscout container must read this disposable synthetic key.
    (directory / 'test.key').chmod(0o644)
    return directory


if __name__ == '__main__':
    print(generate(sys.argv[1]))
