import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class LocalRequestPolicyTests(unittest.TestCase):
    def test_cross_site_and_rebinding_requests_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / 'test_local_request_policy')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Iinclude',
                            'tests/test_local_request_policy.cpp', '-fsanitize=address,undefined', '-o', exe],
                           cwd=ROOT, check=True)
            subprocess.run([exe], check=True)

    def test_guard_is_registered_before_every_api_route(self):
        source = (ROOT / 'src/web_server.cpp').read_text()
        init = source[source.index('void webserver_init()'):]
        guard = init.index('.setFilter(api_request_refused)')
        first_route = init.index('server.on("/api/')
        self.assertLess(guard, first_route)
        self.assertLess(init.index('server.on("/api", HTTP_ANY'), init.index('get_web_assets_count()'))


if __name__ == '__main__':
    unittest.main()
