#!/usr/bin/env python3
"""Run the real integration on Apple Silicon without Docker or global installs."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
from make_tls import generate

NIGHTSCOUT_COMMIT = '92d0834219aa771b5837dbcbf1baeb839a200cf6'  # 15.0.8
MONGO_URL = 'https://fastdl.mongodb.org/osx/mongodb-macos-arm64-7.0.16.tgz'
MONGO_SHA256 = 'e01c5ce1ef8efbef4196797130564b8f121f6e29f20ccf70e025feba07e15739'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parser', required=True)
    parser.add_argument('--cache-dir', type=Path, default=Path(tempfile.gettempdir()) / 'sugarclock-nightscout-runtime')
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        parser.error('This launcher supports Apple Silicon macOS. Use compose.yml elsewhere.')
    for command in ('git', 'node', 'npm'):
        if not shutil.which(command):
            parser.error(command + ' must already be installed')
    cache = args.cache_dir.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    source = cache / 'nightscout'
    if not source.exists():
        subprocess.run(['git', 'clone', '--depth', '1', '--branch', '15.0.8',
                        'https://github.com/nightscout/cgm-remote-monitor.git', str(source)], check=True)
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
    assert commit == NIGHTSCOUT_COMMIT, 'Unexpected upstream checkout; choose a fresh cache directory'
    if not (source / 'node_modules/.cache/_ns_cache/public').exists():
        subprocess.run(['npm', 'ci', '--no-audit', '--no-fund'], cwd=source, check=True)
    archive = cache / 'mongodb.tgz'
    if not archive.exists():
        urllib.request.urlretrieve(MONGO_URL, archive)
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == MONGO_SHA256, 'MongoDB archive checksum mismatch'
    mongod = cache / 'mongodb-macos-aarch64-7.0.16/bin/mongod'
    if not mongod.exists():
        # Archive is pinned by checksum before extraction.
        with tarfile.open(archive) as tar:
            tar.extractall(cache)
    tls = generate(cache / 'tls')
    processes = []
    logs = []
    with tempfile.TemporaryDirectory(prefix='sugarclock-nightscout-data-') as data:
        try:
            mongo_log = open(cache / 'mongo.log', 'w')
            logs.append(mongo_log)
            processes.append(subprocess.Popen([str(mongod), '--dbpath', data, '--bind_ip', '127.0.0.1',
                                                '--port', '27027'], stdout=mongo_log, stderr=subprocess.STDOUT))
            for roles, port in [('denied', '13371'), ('readable', '13372')]:
                inherited = {key: os.environ[key] for key in ('PATH', 'HOME', 'TMPDIR', 'LANG') if key in os.environ}
                env = dict(inherited, PORT=port, NIGHTSCOUT_HOSTNAME='127.0.0.1',
                           MONGO_CONNECTION='mongodb://127.0.0.1:27027/sugarclock_' + roles,
                           API_SECRET='sugarclock-synthetic-test-only', AUTH_DEFAULT_ROLES=roles,
                           SSL_KEY=str(tls / 'test.key'), SSL_CERT=str(tls / 'test.crt'),
                           NODE_ENV='production', ENABLE='')
                log = open(cache / (roles + '.log'), 'w')
                logs.append(log)
                processes.append(subprocess.Popen(['node', 'lib/server/server.js'], cwd=source, env=env,
                                                  stdout=log, stderr=subprocess.STDOUT))
            time.sleep(1)
            if any(process.poll() is not None for process in processes):
                raise RuntimeError('A server exited; inspect logs in ' + str(cache))
            subprocess.run([sys.executable, str(Path(__file__).with_name('run.py')),
                            '--parser', str(Path(args.parser).resolve()), '--ca-file', str(tls / 'test.crt')], check=True)
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
            for process in reversed(processes):
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            for log in logs:
                log.close()
    print('Temporary databases removed; cached source, dependencies and logs: ' + str(cache))


if __name__ == '__main__':
    main()
