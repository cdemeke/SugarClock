import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class StatusIdentityTests(unittest.TestCase):
    def test_installation_id_is_only_public_after_fleet_registration(self):
        source = (ROOT / 'src/web_server.cpp').read_text()
        status = source[source.index('static void handle_status('):source.index('\n}\n', source.index('static void handle_status('))]
        exposures = re.findall(r'^.*fleet_installation_id\(\).*$', source, re.M)
        self.assertEqual(len(exposures), 1, 'installation ID must only appear in /api/status')
        self.assertIn('if (fleet_is_registered()) doc["installation_id"] = fleet_installation_id();', status)


if __name__ == '__main__':
    unittest.main()
