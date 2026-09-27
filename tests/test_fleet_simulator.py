import os
import tempfile
import unittest
from unittest import mock
from urllib.error import HTTPError
from fleet import simulator as sim


class SimulatorTests(unittest.TestCase):
    def setUp(self):
        self.clock = sim.new_clock(0)
        self.offer = dict(target_id='target', manifest_url='https://example.com/v1.json',
                          version='1.0.0', channel='stable', sha256='a' * 64)

    def test_denied_expired_and_changed_authorization_never_install(self):
        for response in ({'authorized': False},
                         {'authorized': True, 'expires_at': 0, 'update_offer': self.offer},
                         {'authorized': True, 'expires_at': 9999999999,
                          'update_offer': {**self.offer, 'sha256': 'b' * 64}}):
            with self.subTest(response=response), mock.patch.object(sim, 'api', return_value=response):
                self.assertFalse(sim.process_offer('http://local', self.clock, self.offer, True))
                self.assertEqual(self.clock['firmware_version'], '0.2.2')

    def test_pause_http_denial_never_installs(self):
        with mock.patch.object(sim, 'api', side_effect=HTTPError('url', 409, 'paused', {}, None)):
            with self.assertRaises(HTTPError):
                sim.process_offer('http://local', self.clock, self.offer, True)
        self.assertEqual(self.clock['firmware_version'], '0.2.2')

    def test_fake_update_is_explicit_and_retries_lost_ack(self):
        authorized = {'authorized': True, 'expires_at': 9999999999, 'update_offer': self.offer}
        with mock.patch.object(sim, 'api', side_effect=[authorized, OSError('offline')]):
            with self.assertRaises(OSError):
                sim.process_offer('http://local', self.clock, self.offer, True)
        self.assertEqual(self.clock['firmware_version'], '1.0.0')
        self.assertIn('pending_update', self.clock)
        with mock.patch.object(sim, 'api', return_value={}) as call:
            sim.report_pending('http://local', self.clock)
            self.assertEqual(call.call_args.args[3]['status'], 'boot_validated')
        self.assertNotIn('pending_update', self.clock)

    def test_default_simulator_defers_and_legacy_command_cannot_install(self):
        with mock.patch.object(sim, 'api', return_value={}) as call:
            self.assertFalse(sim.process_offer('http://local', self.clock, self.offer))
            self.assertTrue(call.call_args.args[1].endswith('/result'))
        status, reason = sim.process_command(self.clock, dict(id='old', type='ota_install', payload=self.offer))
        self.assertEqual((status, reason), ('failed', 'requires_rollout_target'))
        self.assertEqual(self.clock['firmware_version'], '0.2.2')

    def test_server_interval_and_feature_capability_payload(self):
        with mock.patch.object(sim, 'api', return_value={'commands': [], 'next_checkin_seconds': 320}) as call, \
             mock.patch.object(sim.time, 'time', return_value=1000), mock.patch.object(sim.random, 'randint', return_value=0):
            sim.check_in('http://local', self.clock, features=True)
            self.assertEqual(self.clock['next_checkin_at'], 1320)
            self.assertEqual(call.call_args.args[3]['capabilities'], ['fleet_rollout_v1'])
            self.assertEqual(call.call_args.args[3]['features']['data_source'], 'demo')

    def test_auth_loss_preserves_identity_for_reregistration(self):
        self.clock['registered'] = True
        installation = self.clock['installation_id']
        with mock.patch.object(sim, 'api', side_effect=HTTPError('url', 401, 'deleted', {}, None)):
            with self.assertRaises(HTTPError):
                sim.visit('http://local', self.clock)
        self.assertFalse(self.clock['registered'])
        self.assertEqual(self.clock['installation_id'], installation)

    def test_state_is_private_and_reduced_count_preserves_identities(self):
        with tempfile.TemporaryDirectory() as temp:
            path = os.path.join(temp, 'clocks.json')
            sim.save_state(path, [self.clock, sim.new_clock(1)])
            self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
            self.assertEqual(len(sim.load_state(path, 1)), 2)
