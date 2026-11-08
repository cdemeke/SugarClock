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
from fleet.sugarfleet.telemetry import overview


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

    @unittest.skipUnless(shutil.which('node'), 'Node is required for frontend behavior tests')
    def test_overview_rendering_behavior(self):
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run(['node', '--test', 'tests/test_fleet_overview.cjs'],
                                cwd=root, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_navigation_identifies_current_page(self):
        for path, label in (('/admin/overview', 'Overview'), ('/admin/devices', 'Devices'),
                            ('/admin/devices/1', 'Devices'), ('/admin/releases', 'Releases')):
            response = self.get(path)
            self.assertEqual([re.sub(r'<[^>]+>', '', text) for text in re.findall(r'aria-current="page">(.*?)</a>', response.text)], [label])

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

    def test_devices_table_renders_data_location_for_filtering(self):
        response = self.get('/admin/devices')
        self.assertEqual(response.status_code, 200)
        self.assertIn('data-location="Boston, US"', response.text)
        self.assertIn('data-location="London, GB"', response.text)
        self.assertIn('data-location="Toronto, CA"', response.text)

    def test_breakdowns_use_checkin_windows_and_exclude_disabled_and_unseen_clocks(self):
        now = 2_000_000_000
        with self.app.app_context():
            connection = get_db()
            connection.execute('UPDATE devices SET last_checkin_at=NULL')
            for device_id, age in enumerate((0, 7, 30, 90, 91), start=1):
                connection.execute('UPDATE devices SET last_checkin_at=?, features_json=?, detected_city=?, detected_region=?, detected_country_code=? WHERE id=?',
                    (now - age * 86400, json.dumps({'weather_enabled': True}), 'Portland', 'Oregon' if device_id == 1 else 'Maine', 'US', device_id))
            connection.execute('UPDATE devices SET features_json=NULL, detected_city="", detected_region="", detected_country_code="" WHERE id=5')
            connection.execute('UPDATE devices SET last_checkin_at=?, blocked_at=? WHERE id=6', (now, now))
            connection.execute('UPDATE devices SET last_checkin_at=?, retired_at=? WHERE id=7', (now, now))
            connection.commit()
            data = overview(connection, now)
        for days, total in (('7', 2), ('30', 3), ('90', 4), ('all', 5)):
            with self.subTest(days=days):
                breakdown = data['breakdowns'][days]
                self.assertEqual(breakdown['feature_denominator'], total)
                self.assertEqual(sum(breakdown['location_counts'].values()), total)
                self.assertEqual(breakdown['location_counts']['Portland, OR, US'], 1)
                self.assertEqual(breakdown['feature_counts']['weather_enabled'],
                    {'enabled': min(total, 4), 'known': min(total, 4), 'unknown': int(days == 'all')})
        self.assertEqual(data['location_counts'], data['breakdowns']['30']['location_counts'])
        self.assertEqual(data['breakdowns']['all']['location_counts']['Unknown'], 1)
        page = self.get('/admin/devices')
        self.assertIn('data-location="Portland, OR, US"', page.text)
        self.assertIn('data-location="Portland, ME, US"', page.text)


if __name__ == '__main__':
    unittest.main()
