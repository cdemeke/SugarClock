"""Rendered dashboard contracts and isolated demo safeguards."""
import re
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from fleet.demo import build_demo
from fleet.sugarfleet.db import get_db


class FleetDashboardTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.app = build_demo(Path(self.directory.name) / 'demo.db', 8081)
        self.client = self.app.test_client()

    def tearDown(self):
        self.directory.cleanup()

    def get(self, path, **kwargs):
        return self.client.get(path, base_url='http://127.0.0.1:8081', **kwargs)

    def test_all_pages_render_and_scripts_parse(self):
        for path in ('/admin/overview', '/admin/devices', '/admin/devices/1', '/admin/releases'):
            response = self.get(path)
            self.assertEqual(response.status_code, 200, response.text)
            self.assertNotIn('Pending approval', response.text)
            self.assertNotIn('Payload (JSON)', response.text)
            if shutil.which('node'):
                scripts = re.findall(r'<script(?:\s[^>]*)?>(.*?)</script>', response.text, re.S)
                for script in scripts:
                    result = subprocess.run(['node', '--check'], input=script, text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, f'{path}: {result.stderr}')

    def test_nickname_is_escaped_and_identity_stays_stable(self):
        with self.app.app_context():
            connection = get_db()
            identity = connection.execute('SELECT installation_id FROM devices WHERE id=1').fetchone()[0]
            connection.execute('UPDATE devices SET friendly_name=? WHERE id=1', ('<script>alert(1)</script>',))
            connection.commit()
        response = self.get('/admin/devices/1')
        self.assertEqual(response.status_code, 200)
        self.assertNotIn('<script>alert(1)</script>', response.text)
        self.assertIn(identity, response.text)

    def test_demo_rejects_external_hosts_and_device_traffic(self):
        self.assertEqual(self.client.get('/admin/overview', base_url='http://evil.example:8081').status_code, 403)
        self.assertEqual(self.get('/admin/overview', environ_overrides={'REMOTE_ADDR':'192.0.2.1'}).status_code, 403)
        self.assertEqual(self.get('/device/v1/check-in').status_code, 403)
        self.assertEqual(self.client.post('/admin/api/releases/sync', base_url='http://127.0.0.1:8081').status_code, 403)

    def test_demo_state_changes_require_csrf(self):
        self.get('/admin/overview')
        denied = self.client.patch('/admin/api/rollouts/1', json={'percentage':100}, base_url='http://127.0.0.1:8081')
        self.assertEqual(denied.status_code, 403)
        page = self.get('/admin/overview')
        token = json.loads(re.search(r'window.fleetCsrf = (.*?);', page.text).group(1))
        response = self.client.patch('/admin/api/rollouts/1', json={'percentage':100},
            base_url='http://127.0.0.1:8081', headers={'X-CSRF-Token':token})
        self.assertEqual(response.status_code, 200, response.text)


if __name__ == '__main__':
    unittest.main()
