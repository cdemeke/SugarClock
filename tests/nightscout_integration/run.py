#!/usr/bin/env python3
"""Exercise actual Nightscout using synthetic data and the production C++ parser.

This deliberately refuses remote targets and a nonmatching server version.
It creates and removes a temporary read-only subject; fixtures remain in the
isolated test database until the disposable services are removed.
"""
import argparse
import hashlib
import json
import subprocess
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

SECRET = 'sugarclock-synthetic-test-only'
VERSION = '15.0.8'
TLS_CONTEXT = None


def request(url, credential=None, body=None, method=None):
    headers = {'Accept': 'application/json'}
    if credential:
        headers['api-secret'] = credential
    if body is not None:
        headers['Content-Type'] = 'application/json'
        body = json.dumps(body).encode()
    req = urllib.request.Request(url, headers=headers, data=body, method=method)
    try:
        with urllib.request.urlopen(req, timeout=10, context=TLS_CONTEXT) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def expect(status, allowed, name):
    assert status in allowed, f'{name}: unexpected HTTP {status}; expected {allowed}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--denied-url', default='https://127.0.0.1:13371')
    parser.add_argument('--public-url', default='https://127.0.0.1:13372')
    parser.add_argument('--parser', required=True, help='Compiled tests/test_nightscout.cpp executable')
    parser.add_argument('--ca-file', required=True, help='Temporary local test CA certificate')
    args = parser.parse_args()
    global TLS_CONTEXT
    TLS_CONTEXT = ssl.create_default_context(cafile=args.ca_file)
    for url in (args.denied_url, args.public_url):
        parts = urllib.parse.urlsplit(url)
        assert parts.scheme == 'https', 'Test requests require HTTPS'
        assert parts.hostname in ('127.0.0.1', 'localhost', '::1'), 'Only local synthetic test servers are allowed'
        assert not parts.username and not parts.password and not parts.query and not parts.fragment

    admin = hashlib.sha1(SECRET.encode()).hexdigest()
    checks = []

    def passed(name):
        checks.append(name)
        print('PASS:', name)

    for base in (args.denied_url, args.public_url):
        deadline = time.monotonic() + 120
        while True:
            try:
                status, body = request(base + '/api/v1/status.json', admin)
                if status == 200:
                    assert json.loads(body)['version'] == VERSION
                    break
            except (OSError, urllib.error.URLError):
                pass
            if time.monotonic() >= deadline:
                raise RuntimeError('Nightscout readiness deadline exceeded')
            time.sleep(1)
    passed('both actual Nightscout instances report version ' + VERSION)

    now = int(time.time())
    # Both values and times are artificial, independent of any CGM or patient.
    fixtures = [dict(type='sgv', sgv=value, date=stamp * 1000,
                     dateString=time.strftime('%Y-%m-%dT%H:%M:%S.000Z', time.gmtime(stamp)),
                     direction=direction, device='SugarClock synthetic integration')
                for value, stamp, direction in [(100, now - 300, 'Flat'), (110, now, 'SingleUp')]]
    for base in (args.denied_url, args.public_url):
        status, _ = request(base + '/api/v1/entries', admin, fixtures)
        expect(status, (200, 201), 'seed synthetic readings')
    passed('API-secret SHA-1 authentication creates synthetic readings')

    def fetch(base, credential=None, mode=None):
        if mode is None:
            mode = 1 if credential else 0
        generated = subprocess.run([args.parser, '--request', base, str(mode), credential or ''],
                                   capture_output=True, text=True, check=True)
        config = json.loads(generated.stdout)
        assert config['url'] == base + '/api/v1/entries/sgv.json?count=3', config
        assert 'token=' not in config['url'] and SECRET not in config['url']
        if mode == 2:
            assert config['header'] == admin
        return request(config['url'], config['header'])

    status, _ = fetch(args.denied_url)
    expect(status, (401, 403), 'private anonymous access')
    passed('denied instance rejects anonymous reads')
    for credential in ('invalid-synthetic-token', hashlib.sha1(b'wrong-secret').hexdigest(), SECRET):
        status, _ = fetch(args.denied_url, credential)
        expect(status, (401, 403), 'incorrect credential')
    passed('wrong token, wrong secret hash, and unhashed secret are rejected')

    subject_id = None
    try:
        name = 'sugarclock-' + uuid.uuid4().hex[:8]
        status, body = request(args.denied_url + '/api/v2/authorization/subjects', admin,
                               {'name': name, 'roles': ['readable']})
        expect(status, (200, 201), 'create read-only subject')
        subject_id = json.loads(body)['_id']
        status, body = request(args.denied_url + '/api/v2/authorization/subjects', admin)
        expect(status, (200,), 'list subjects')
        token = next(item['accessToken'] for item in json.loads(body) if item['_id'] == subject_id)
        for base, credential, label, mode in [(args.public_url, None, 'public', 0),
                                        (args.denied_url, SECRET, 'API secret', 2),
                                        (args.denied_url, token, 'read-only token', 1)]:
            status, body = fetch(base, credential, mode)
            expect(status, (200,), label + ' read')
            result = subprocess.run([args.parser, '--parse', str(now)], input=body,
                                    capture_output=True, text=True, check=True)
            parsed = json.loads(result.stdout)
            assert parsed['glucose'] == 110, parsed
            assert parsed['trend'] == 1, parsed
            assert parsed['timestamp'] == now, parsed
            assert parsed['age_sec'] == 0, parsed
            assert parsed['has_previous'], parsed
            assert parsed['previous_glucose'] == 100, parsed
            assert parsed['previous_timestamp'] == now - 300, parsed
            assert parsed['glucose'] - parsed['previous_glucose'] == 10, parsed
            # Re-fetching a frozen response must not reset its measurement age.
            repeated = subprocess.run([args.parser, '--parse', str(now + 1200)], input=body,
                                      capture_output=True, text=True, check=True)
            assert json.loads(repeated.stdout)['age_sec'] == 1200
            passed(label + ' response parses to 110 mg/dL, +10 delta, correct sensor time and aging')
        status, _ = request(args.denied_url + '/api/v1/entries', token, fixtures)
        expect(status, (401, 403), 'read-only write rejection')
        status, _ = request(args.public_url + '/api/v1/entries', None, fixtures)
        expect(status, (401, 403), 'public anonymous write rejection')
        passed('read-only token and anonymous public access cannot upload readings')
        status, _ = request(args.denied_url + '/api/v2/authorization/subjects/' + subject_id,
                            admin, method='DELETE')
        expect(status, (200, 204), 'revoke token')
        subject_id = None
        status, _ = fetch(args.denied_url, token)
        expect(status, (401, 403), 'revoked token')
        passed('revoked token is rejected')
    finally:
        if subject_id:
            request(args.denied_url + '/api/v2/authorization/subjects/' + subject_id,
                    admin, method='DELETE')
    print(json.dumps({'nightscout_version': VERSION, 'checks_passed': len(checks),
                      'synthetic_latest_glucose': 110, 'synthetic_delta': 10}, indent=2))


if __name__ == '__main__':
    main()
