#!/usr/bin/env python3
"""Exercise an isolated, real HTTP fleet service; never connects to production.

Run: python -m fleet.load_test --devices 1000 --seconds 90 --rate 12
Uses a temporary SQLite database and one Gunicorn process with eight threads.
"""
import argparse
import concurrent.futures
import json
import math
import os
from pathlib import Path
import secrets
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.request
import uuid

from fleet.sugarfleet import create_app, rollouts
from fleet.sugarfleet.db import get_db
from fleet.sugarfleet.security import hash_device_credential


def run(args):
    with tempfile.TemporaryDirectory(prefix='sugarfleet-load-') as directory:
        database = str(Path(directory) / 'fleet.db')
        secret = secrets.token_hex(32)
        config = {'TESTING': True, 'DATABASE': database, 'SECRET_KEY': secret,
                  'DEVICE_CREDENTIAL_PEPPER': secret, 'IP_GEOLOCATION_ENABLED': False}
        app = create_app(config)
        clocks = [(str(uuid.uuid4()), secrets.token_urlsafe(32)) for _ in range(args.devices)]
        now = int(time.time())
        with app.app_context():
            connection = get_db()
            connection.executemany(
                "INSERT INTO devices(installation_id,credential_hash,hardware,management_protocol,first_seen,last_seen,firmware_version,channel,timezone,verification_state) VALUES(?,?,?,1,?,?,?,'stable','UTC','verified')",
                [(identity, hash_device_credential(credential, secret), 'ulanzi-tc001-esp32-4mb', now, now, '0.3.0') for identity, credential in clocks])
            for index, (identity, _) in enumerate(clocks):
                connection.execute('UPDATE devices SET capabilities_json=?,last_checkin_at=? WHERE installation_id=?',
                    (json.dumps(['fleet_rollout_v1'] if index % 2 else []), now, identity))
            connection.execute("INSERT INTO releases(version,channel,manifest_url,firmware_url,firmware_sha256,firmware_size,published_at,imported_at,approved_by,metadata_complete) VALUES('0.4.0','stable','https://example.invalid/releases/v0.4.0/manifest.json','https://example.invalid/releases/v0.4.0/firmware.bin',?,1000000,'2026-09-12T00:00:00Z',?,'load-test',1)", ('0'*64, now))
            connection.commit()
            rollouts.create(connection, {'release_id': 1, 'kind': 'stable', 'percentage': 100}, 'load-test')
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            port = listener.getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        environment = dict(os.environ, FLEET_DATABASE=database, FLEET_SECRET_KEY=secret,
                           FLEET_DEVICE_CREDENTIAL_PEPPER=secret, FLEET_INSECURE_COOKIES='1',
                           FLEET_IP_GEOLOCATION_ENABLED='0', FLEET_TRUSTED_PROXY_HOPS='0')
        with open(Path(directory) / 'server.log', 'w+') as log:
            server = subprocess.Popen([sys.executable, '-m', 'gunicorn', '--bind', f'127.0.0.1:{port}',
                                       '--workers', '1', '--threads', '8', '--timeout', '30',
                                       'fleet.sugarfleet.__main__:app'], env=environment, stdout=log, stderr=log)
            try:
                for _ in range(100):
                    try:
                        with urllib.request.urlopen(base + '/healthz', timeout=1):
                            break
                    except OSError:
                        if server.poll() is not None:
                            log.seek(0)
                            raise RuntimeError(log.read())
                        time.sleep(.1)
                else:
                    raise RuntimeError('isolated fleet server did not start')

                def checkin(index):
                    identity, credential = clocks[index % len(clocks)]
                    body = {'installation_id': identity, 'firmware_version': '0.3.0',
                            'channel': 'stable', 'uptime_seconds': 1000 + index}
                    if index % 2:
                        body.update(capabilities=['fleet_rollout_v1'], features={
                            'schema_version': 1, 'data_source': 'dexcom' if index % 3 else 'libre',
                            'weather_enabled': True, 'companion_enabled': bool(index % 3),
                            'companion_character': index % 7})
                    request = urllib.request.Request(base + '/device/v1/check-in',
                        data=json.dumps(body).encode(), headers={'Content-Type': 'application/json',
                        'Authorization': 'Bearer ' + credential})
                    start = time.monotonic()
                    with urllib.request.urlopen(request, timeout=20) as response:
                        result = json.load(response)
                    if 'commands' not in result:
                        raise RuntimeError('missing check-in contract')
                    return time.monotonic() - start

                def phase(count, paced):
                    latencies, errors = [], []
                    start = time.monotonic()
                    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
                        pending = []
                        for index in range(count):
                            if paced:
                                time.sleep(max(0, start + index / args.rate - time.monotonic()))
                            pending.append(pool.submit(checkin, index))
                        for task in concurrent.futures.as_completed(pending):
                            try:
                                latencies.append(task.result())
                            except Exception as error:
                                errors.append(str(error))
                    elapsed = time.monotonic() - start
                    latencies.sort()
                    return {'requests': count, 'errors': len(errors), 'error_samples': errors[:3],
                            'seconds': round(elapsed, 2), 'requests_per_second': round(count / elapsed, 2),
                            'p95_ms': round(latencies[max(0, math.ceil(.95 * len(latencies)) - 1)] * 1000, 2) if latencies else None,
                            'max_ms': round(max(latencies) * 1000, 2) if latencies else None}

                report = {'devices': args.devices, 'population': '50% legacy, 50% fleet-aware, active stable rollout',
                          'server': '1 Gunicorn worker / 8 threads / SQLite WAL',
                          'paced': phase(math.ceil(args.seconds * args.rate), True),
                          'reconnect_burst': phase(args.devices, False)}
                source = sqlite3.connect(database)
                backup = sqlite3.connect(str(Path(directory) / 'restored.db'))
                source.backup(backup)
                integrity = backup.execute('PRAGMA integrity_check').fetchone()[0]
                count = backup.execute('SELECT COUNT(*) FROM devices WHERE last_checkin_at IS NOT NULL').fetchone()[0]
                report['backup_restore'] = {'integrity': integrity, 'reporting_devices': count}
                source.close()
                backup.close()
                print(json.dumps(report, indent=2))
                if args.output:
                    Path(args.output).write_text(json.dumps(report, indent=2) + '\n')
                if report['paced']['errors'] or report['reconnect_burst']['errors'] or integrity != 'ok' or count != args.devices:
                    raise SystemExit(1)
            finally:
                server.terminate()
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--devices', type=int, default=1000)
    parser.add_argument('--seconds', type=float, default=90)
    parser.add_argument('--rate', type=float, default=12)
    parser.add_argument('--output')
    args = parser.parse_args()
    if args.devices < 1 or args.devices > 10000 or args.seconds <= 0 or args.rate <= 0:
        parser.error('devices must be 1–10000 and seconds/rate must be positive')
    run(args)


if __name__ == '__main__':
    main()
