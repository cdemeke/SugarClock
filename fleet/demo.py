#!/usr/bin/env python3
"""Disposable local dashboard with synthetic clocks. No production credentials/data.

Run: python -m fleet.demo
Open http://127.0.0.1:8081/admin/overview. All changes disappear on exit.
"""
import argparse
import json
from pathlib import Path
import secrets
import tempfile
import time
import uuid

from flask import abort, request, session
from fleet.sugarfleet import create_app
from fleet.sugarfleet.db import get_db
from fleet.sugarfleet import rollouts
from fleet.sugarfleet.security import hash_device_credential


def build_demo(database, port):
    secret = secrets.token_hex(32)
    app = create_app({'TESTING': True, 'DATABASE': str(database), 'SECRET_KEY': secret,
                      'DEVICE_CREDENTIAL_PEPPER': secret, 'GITHUB_ALLOWLIST': 'local-demo',
                      'SESSION_COOKIE_SECURE': False, 'IP_GEOLOCATION_ENABLED': False,
                      'TRUSTED_PROXY_HOPS': 0, 'GITHUB_CLIENT_ID': '', 'GITHUB_CLIENT_SECRET': '',
                      'GITHUB_API_TOKEN': ''})

    @app.before_request
    def local_session():
        if request.remote_addr not in {'127.0.0.1', '::1'} or request.host not in {f'127.0.0.1:{port}', f'localhost:{port}'}:
            abort(403)
        if request.path.startswith('/device/') or request.path.startswith('/auth/'):
            abort(403)
        if request.path.endswith('/releases/sync') or request.path.endswith('/releases/import'):
            abort(403, 'Synthetic demo does not import real releases')
        session['github_login'] = 'local-demo'
        session.setdefault('csrf_token', secrets.token_urlsafe(32))

    now = int(time.time())
    with app.app_context():
        connection = get_db()
        for index in range(24):
            identity = str(uuid.uuid4())
            nickname = ['My desk', 'Kitchen clock', 'Test bench'][index] if index < 3 else ''
            age = [30, 150, 260, 900, 5000, 86400, 14*86400, 40*86400][index % 8]
            features = {'schema_version': 1, 'data_source': ['dexcom', 'libre', 'custom'][index % 3],
                        'weather_enabled': index % 3 != 0, 'companion_enabled': index % 4 != 0,
                        'companion_character': index % 7, 'timer_enabled': index % 2 == 0,
                        'stopwatch_enabled': False, 'auto_cycle_enabled': True, 'countdown_enabled': False}
            connection.execute("INSERT INTO devices(installation_id,credential_hash,hardware,management_protocol,first_seen,last_seen,last_checkin_at,firmware_version,channel,timezone,verification_state,friendly_name,capabilities_json,features_json,features_reported_at,detected_city,detected_country_code) VALUES(?,?,?,1,?,?,?,'0.3.0','stable','UTC','verified',?,?,?,?,?,?)",
                (identity, hash_device_credential(secrets.token_urlsafe(32), secret), 'ulanzi-tc001-esp32-4mb',
                 now-60*86400, now-age, now-age, nickname, json.dumps(['fleet_rollout_v1'] if index < 20 else []),
                 json.dumps(features) if index < 20 else None, now-age if index < 20 else None,
                 ['Boston','London','Toronto'][index%3], ['US','GB','CA'][index%3]))
        for version, channel in [('0.4.0','stable'), ('0.5.0','preview'), ('0.5.0','stable')]:
            connection.execute("INSERT INTO releases(version,channel,manifest_url,firmware_url,firmware_sha256,firmware_size,published_at,imported_at,approved_by,metadata_complete) VALUES(?,?,?,?,?,1000000,?,?,?,1)",
              (version, channel, f'https://example.invalid/releases/v{version}-{channel}/ota-manifest.json',
               f'https://example.invalid/releases/v{version}-{channel}/firmware.bin', '0'*64,
               '2026-09-12T00:00:00Z', now, 'synthetic-demo'))
        connection.commit()
        rollouts.create(connection, {'release_id':1, 'kind':'stable', 'percentage':10}, 'local-demo')
    return app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8081)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('port must be 1024–65535')
    with tempfile.TemporaryDirectory(prefix='sugarfleet-demo-') as directory:
        app = build_demo(Path(directory) / 'demo.db', args.port)
        print('Synthetic local demo. Releases cannot reach real devices. Database is removed on exit.', flush=True)
        app.run(host='127.0.0.1', port=args.port, debug=False)


if __name__ == '__main__':
    main()
