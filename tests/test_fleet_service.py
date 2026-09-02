import base64
import json
import os
import subprocess
import tempfile
import time
import unittest
from unittest import mock

from fleet import create_app
from fleet.sugarfleet import auth
from fleet.sugarfleet.db import get_db
from fleet.sugarfleet.geolocation import location_label, public_ip
from fleet.sugarfleet.releases import canonical_payload
from fleet.sugarfleet.security import hash_device_credential, verify_device_credential
from fleet.sugarfleet.util import ApiError, connectivity_state
from fleet.sugarfleet.validation import (
    COMMAND_TYPES,
    validate_checkin,
    validate_command,
    validate_register,
    validate_result,
)


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join(ROOT, "fleet", "protocol", "v1", "fixtures")
INSTALLATION_ID = "0d9189e2-117a-42b8-95c1-8fc88f23a8a4"
CREDENTIAL = "MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY"


def fixture(name):
    with open(os.path.join(FIXTURES, name), encoding="utf-8") as stream:
        return json.load(stream)


class FleetProtocolFixtureTests(unittest.TestCase):
    def test_ambient_cat_config_patch_is_validated(self):
        validate_command(
            "config_patch",
            {"changes": {"ambient_enabled": True, "ambient_seasonal": False, "default_mode": 3}},
        )
        with self.assertRaisesRegex(ApiError, "default_mode is out of range"):
            validate_command("config_patch", {"changes": {"default_mode": 4}})

    def test_missing_production_secret_fails_closed(self):
        with mock.patch.dict(os.environ, {"FLEET_SECRET_KEY": "", "FLEET_INSECURE_COOKIES": ""}):
            with self.assertRaisesRegex(RuntimeError, "FLEET_SECRET_KEY is required"):
                create_app({"TESTING": False})

    def test_random_device_credentials_use_fast_keyed_digest(self):
        pepper = "test-device-pepper-with-at-least-32-bytes"
        encoded = hash_device_credential(CREDENTIAL, pepper)
        self.assertTrue(encoded.startswith("hmac-sha256$"))
        self.assertTrue(verify_device_credential(CREDENTIAL, encoded, pepper))
        self.assertFalse(verify_device_credential("A" * 43, encoded, pepper))

    def test_registration_checkin_results_and_unknown_fields(self):
        validate_register(fixture("register-request.json"))
        validate_checkin(fixture("check-in-request.json"))
        self.assertEqual(fixture("register-response.json")["status"], "registered")
        self.assertEqual(fixture("check-in-response.json")["commands"][0]["type"], "notify")
        for result in fixture("command-results.json"):
            validate_result(result)

    def test_every_command_type_has_a_valid_fixture(self):
        commands = fixture("commands.json")
        self.assertEqual({command["type"] for command in commands}, COMMAND_TYPES)
        for command in commands:
            validate_command(command["type"], command["payload"])

    def test_connectivity_thresholds(self):
        now = 2_000_000_000
        self.assertEqual(connectivity_state(now - 299, now=now), "online")
        self.assertEqual(connectivity_state(now - 300, now=now), "delayed")
        self.assertEqual(connectivity_state(now - 1800, now=now), "offline")
        self.assertEqual(connectivity_state(now - 30 * 86400, now=now), "dormant")
        self.assertEqual(connectivity_state(now, retired_at=now, now=now), "retired")

    def test_geolocation_accepts_only_public_ips_and_formats_coarse_labels(self):
        self.assertEqual(public_ip("8.8.8.8"), "8.8.8.8")
        self.assertIsNone(public_ip("192.168.1.55"))
        self.assertIsNone(public_ip("127.0.0.1"))
        self.assertIsNone(public_ip("not-an-ip"))
        self.assertEqual(location_label("Boston", "Massachusetts", "US"), "Boston, Massachusetts, US")
        self.assertEqual(location_label("Singapore", "Singapore", "SG"), "Singapore, SG")


class FleetServiceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.app = create_app(
            {
                "TESTING": True,
                "DATABASE": os.path.join(self.temp.name, "fleet.db"),
                "SECRET_KEY": "test-secret-key-with-at-least-32-bytes",
                "DEVICE_CREDENTIAL_PEPPER": "test-device-pepper-with-at-least-32-bytes",
                "GITHUB_ALLOWLIST": ["admin"],
                "SESSION_COOKIE_SECURE": False,
                "DEVICE_CHECKIN_SECONDS": 120,
                "OTA_PUBLIC_KEYS_DIR": self.temp.name,
                "ENROLLMENT_REQUIRED": False,
                "IP_GEOLOCATION_SYNCHRONOUS": True,
            }
        )
        self.client = self.app.test_client()

    def tearDown(self):
        self.temp.cleanup()

    def auth_headers(self, credential=CREDENTIAL):
        return {"Authorization": "Bearer " + credential}

    def admin_headers(self):
        with self.client.session_transaction() as session:
            session["github_login"] = "admin"
            session["csrf_token"] = "csrf-test"
        return {"X-CSRF-Token": "csrf-test"}

    def register(self, *, approve=True):
        response = self.client.post(
            "/device/v1/register",
            json=fixture("register-request.json"),
            headers=self.auth_headers(),
        )
        if approve and response.status_code in (200, 201):
            with self.app.app_context():
                connection = get_db()
                connection.execute("UPDATE devices SET verification_state='verified'")
                connection.commit()
        return response

    def checkin(self):
        return self.client.post(
            "/device/v1/check-in",
            json=fixture("check-in-request.json"),
            headers=self.auth_headers(),
        )

    def test_health_and_sqlite_migration_are_ready(self):
        self.assertEqual(self.client.get("/healthz").json, {"status": "ok"})
        with self.app.app_context():
            versions = get_db().execute("SELECT version FROM schema_migrations").fetchall()
            self.assertEqual(
                [row[0] for row in versions],
                [
                    "0001_initial.sql",
                    "0002_device_location.sql",
                    "0003_detected_location.sql",
                    "0004_reported_identity.sql",
                    "0005_enrollment_and_identity_cleanup.sql",
                ],
            )

    def test_registration_is_idempotent_and_hashes_credential(self):
        first = self.register()
        self.assertEqual(first.status_code, 201)
        self.assertEqual(first.json["status"], "registered")
        self.assertNotIn("commands", first.json)
        second = self.register()
        self.assertEqual(second.status_code, 200)
        self.assertEqual(second.json["status"], "already_registered")
        with self.app.app_context():
            stored = get_db().execute("SELECT credential_hash FROM devices").fetchone()[0]
            self.assertTrue(stored.startswith("hmac-sha256$"))
            self.assertNotIn(CREDENTIAL, stored)

    def test_duplicate_installation_with_wrong_credential_is_rejected(self):
        self.register()
        wrong = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        response = self.client.post(
            "/device/v1/register", json=fixture("register-request.json"), headers=self.auth_headers(wrong)
        )
        self.assertEqual(response.status_code, 409)

    def test_unauthorized_checkin_and_admin_requests_fail(self):
        self.register()
        self.assertEqual(self.client.post("/device/v1/check-in", json=fixture("check-in-request.json")).status_code, 401)
        self.assertEqual(self.client.get("/admin/api/devices").status_code, 401)

    def test_checkin_updates_current_snapshot_without_secret_or_identity_fields(self):
        self.register()
        self.assertEqual(self.checkin().status_code, 200)
        headers = self.admin_headers()
        response = self.client.get("/admin/api/devices/1")
        self.assertEqual(response.status_code, 200)
        device = response.json["device"]
        self.assertEqual(device["connectivity"], "online")
        self.assertEqual(device["battery_percent"], 83)
        self.assertNotIn("reported_nickname", device)
        self.assertNotIn("reported_location", device)
        self.assertNotIn("credential_hash", device)
        self.assertNotIn("wifi_ssid", device)
        self.assertNotIn("glucose", json.dumps(device).lower())

    def test_command_delivery_retry_acknowledgment_and_idempotency(self):
        self.register()
        headers = self.admin_headers()
        command_ids = []
        for command in fixture("commands.json"):
            response = self.client.post(
                "/admin/api/devices/1/commands", json=command, headers=headers
            )
            self.assertEqual(response.status_code, 201, response.json)
            command_ids.append(response.json["command"]["id"])
        first = self.checkin()
        self.assertEqual({command["id"] for command in first.json["commands"]}, set(command_ids))
        second = self.checkin()
        self.assertEqual(len(second.json["commands"]), len(command_ids))
        for command_id in command_ids:
            result = {"installation_id": INSTALLATION_ID, "status": "succeeded"}
            response = self.client.post(
                f"/device/v1/commands/{command_id}/result", json=result, headers=self.auth_headers()
            )
            self.assertEqual(response.status_code, 200)
            repeated = self.client.post(
                f"/device/v1/commands/{command_id}/result", json=result, headers=self.auth_headers()
            )
            self.assertEqual(repeated.json["status"], "already_recorded")
        self.assertEqual(self.checkin().json["commands"], [])
        with self.app.app_context():
            attempts = [row[0] for row in get_db().execute("SELECT attempt_count FROM commands")]
            self.assertTrue(all(value == 2 for value in attempts))

    def test_expired_commands_are_not_delivered(self):
        self.register()
        headers = self.admin_headers()
        response = self.client.post(
            "/admin/api/devices/1/commands",
            json={"type": "ota_check", "payload": {}},
            headers=headers,
        )
        command_id = response.json["command"]["id"]
        with self.app.app_context():
            connection = get_db()
            connection.execute("UPDATE commands SET expires_at=? WHERE id=?", (int(time.time()) - 1, command_id))
            connection.commit()
        self.assertEqual(self.checkin().json["commands"], [])

    def test_csrf_and_secret_patch_are_rejected(self):
        self.register()
        self.admin_headers()
        no_csrf = self.client.post(
            "/admin/api/devices/1/commands", json={"type": "ota_check", "payload": {}}
        )
        self.assertEqual(no_csrf.status_code, 403)
        secret = self.client.post(
            "/admin/api/devices/1/commands",
            json={"type": "config_patch", "payload": {"changes": {"wifi_password": "must-not-leak"}}},
            headers={"X-CSRF-Token": "csrf-test"},
        )
        self.assertEqual(secret.status_code, 422)
        self.assertNotIn("must-not-leak", json.dumps(secret.json))

    def test_admin_ui_renders_device_list_and_detail(self):
        self.register()
        self.admin_headers()
        self.assertIn(b"Devices", self.client.get("/admin/devices").data)
        self.assertIn(INSTALLATION_ID.encode(), self.client.get("/admin/devices/1").data)

    def test_admin_can_label_device_by_nickname_and_location(self):
        self.register()
        headers = self.admin_headers()
        response = self.client.patch(
            "/admin/api/devices/1",
            json={"friendly_name": "Kitchen Clock", "location_label": "Boston – Main Office"},
            headers=headers,
        )
        self.assertEqual(response.status_code, 200, response.json)
        self.assertEqual(response.json["device"]["friendly_name"], "Kitchen Clock")
        self.assertEqual(response.json["device"]["location_label"], "Boston – Main Office")

        api_device = self.client.get("/admin/api/devices/1").json["device"]
        self.assertEqual(api_device["friendly_name"], "Kitchen Clock")
        self.assertEqual(api_device["location_label"], "Boston – Main Office")
        self.assertIn(b"Kitchen Clock", self.client.get("/admin/devices").data)
        self.assertIn("Boston – Main Office", self.client.get("/admin/devices/1").text)

        with self.app.app_context():
            audit = get_db().execute(
                "SELECT action, summary_json FROM audit_events ORDER BY id DESC LIMIT 1"
            ).fetchone()
            self.assertEqual(audit["action"], "update_device_identity")
            self.assertEqual(
                json.loads(audit["summary_json"])["changes"]["friendly_name"]["after"],
                "Kitchen Clock",
            )

    def test_device_identity_patch_rejects_unknown_and_invalid_fields(self):
        self.register()
        headers = self.admin_headers()
        unknown = self.client.patch(
            "/admin/api/devices/1", json={"city": "Boston"}, headers=headers
        )
        self.assertEqual(unknown.status_code, 400)
        too_long = self.client.patch(
            "/admin/api/devices/1", json={"location_label": "x" * 121}, headers=headers
        )
        self.assertEqual(too_long.status_code, 400)
        wrong_type = self.client.patch(
            "/admin/api/devices/1", json={"friendly_name": 123}, headers=headers
        )
        self.assertEqual(wrong_type.status_code, 400)

    def test_enrollment_window_and_device_approval_gate_checkins(self):
        self.app.config["ENROLLMENT_REQUIRED"] = True
        closed = self.register(approve=False)
        self.assertEqual(closed.status_code, 403)
        self.assertEqual(closed.json["error"]["code"], "enrollment_closed")

        headers = self.admin_headers()
        opened = self.client.post(
            "/admin/api/enrollment", json={"duration_minutes": 15}, headers=headers
        )
        self.assertEqual(opened.status_code, 200)
        enrolled = self.register(approve=False)
        self.assertEqual(enrolled.status_code, 201)
        self.assertEqual(enrolled.json["verification_state"], "unverified")
        self.assertEqual(self.checkin().status_code, 403)

        approved = self.client.post("/admin/api/devices/1/approve", headers=headers)
        self.assertEqual(approved.json["status"], "verified")
        self.assertEqual(self.checkin().status_code, 200)

    @mock.patch("fleet.sugarfleet.location_worker.lookup_approximate_location")
    def test_public_ip_populates_approximate_location_without_storing_ip(self, lookup):
        lookup.return_value = {
            "city": "Boston",
            "region": "Massachusetts",
            "country_code": "US",
        }
        self.app.config["IP_GEOLOCATION_ENABLED"] = True
        response = self.client.post(
            "/device/v1/register",
            json=fixture("register-request.json"),
            headers=self.auth_headers(),
            environ_overrides={"REMOTE_ADDR": "8.8.8.8"},
        )
        self.assertEqual(response.status_code, 201)
        lookup.assert_called_once_with("8.8.8.8")

        self.admin_headers()
        device = self.client.get("/admin/api/devices/1").json["device"]
        self.assertEqual(device["detected_location"]["label"], "Boston, Massachusetts, US")
        self.assertNotIn("8.8.8.8", json.dumps(device))
        with self.app.app_context():
            columns = [row[1] for row in get_db().execute("PRAGMA table_info(devices)")]
            self.assertFalse(any("ip" in column.lower() for column in columns))

    @mock.patch("fleet.sugarfleet.location_worker.lookup_approximate_location")
    def test_private_ip_and_untrusted_forwarding_header_are_not_geolocated(self, lookup):
        self.app.config["IP_GEOLOCATION_ENABLED"] = True
        response = self.client.post(
            "/device/v1/register",
            json=fixture("register-request.json"),
            headers={**self.auth_headers(), "X-Forwarded-For": "8.8.8.8"},
            environ_overrides={"REMOTE_ADDR": "192.168.1.55"},
        )
        self.assertEqual(response.status_code, 201)
        lookup.assert_not_called()
        self.admin_headers()
        self.assertIsNone(self.client.get("/admin/api/devices/1").json["device"]["detected_location"])

    @mock.patch("fleet.sugarfleet.location_worker.lookup_approximate_location")
    def test_configured_proxy_hop_uses_forwarded_public_ip(self, lookup):
        lookup.return_value = {"city": "Boston", "region": "Massachusetts", "country_code": "US"}
        app = create_app(
            {
                "TESTING": True,
                "DATABASE": os.path.join(self.temp.name, "proxy-fleet.db"),
                "SECRET_KEY": "test-secret-key-with-at-least-32-bytes",
                "DEVICE_CREDENTIAL_PEPPER": "test-device-pepper-with-at-least-32-bytes",
                "GITHUB_ALLOWLIST": ["admin"],
                "SESSION_COOKIE_SECURE": False,
                "OTA_PUBLIC_KEYS_DIR": self.temp.name,
                "IP_GEOLOCATION_ENABLED": True,
                "TRUSTED_PROXY_HOPS": 1,
                "ENROLLMENT_REQUIRED": False,
                "IP_GEOLOCATION_SYNCHRONOUS": True,
            }
        )
        client = app.test_client()
        response = client.post(
            "/device/v1/register",
            json=fixture("register-request.json"),
            headers={**self.auth_headers(), "X-Forwarded-For": "8.8.8.8"},
            environ_overrides={"REMOTE_ADDR": "127.0.0.1"},
        )
        self.assertEqual(response.status_code, 201)
        lookup.assert_called_once_with("8.8.8.8")

    @mock.patch("fleet.sugarfleet.auth._github_json")
    def test_oauth_allows_only_allowlisted_login(self, github_json):
        github_json.side_effect = [{"access_token": "token"}, {"login": "Admin"}]
        with self.client.session_transaction() as session:
            session["oauth_state"] = "state"
        response = self.client.get("/auth/callback?state=state&code=code")
        self.assertEqual(response.status_code, 302)
        with self.client.session_transaction() as session:
            self.assertEqual(session["github_login"], "Admin")
            self.assertTrue(session["csrf_token"])

    def test_signed_release_import_and_immutable_conflict(self):
        private = os.path.join(self.temp.name, "private.pem")
        public = os.path.join(self.temp.name, "ota-test-key-public.pem")
        subprocess.run(["openssl", "genrsa", "-out", private, "2048"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["openssl", "rsa", "-in", private, "-pubout", "-out", public], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        manifest_url = "https://github.com/cdemeke/SugarClock/releases/download/v0.3.0/ota-manifest.json"
        manifest = {
            "schema": 1,
            "product": "sugarclock",
            "hardware": "ulanzi-tc001-esp32-4mb",
            "channel": "stable",
            "version": "0.3.0",
            "minimum_ota_version": "0.2.0",
            "size": 123456,
            "sha256": "a" * 64,
            "firmware_url": "https://github.com/cdemeke/SugarClock/releases/download/v0.3.0/sugarclock-v0.3.0.bin",
            "key_id": "test-key",
            "signature": "",
            "published_at": "2026-08-25T12:00:00Z",
        }
        signed = subprocess.run(
            ["openssl", "dgst", "-sha256", "-sign", private],
            input=canonical_payload(manifest),
            stdout=subprocess.PIPE,
            check=True,
        ).stdout
        manifest["signature"] = base64.b64encode(signed).decode("ascii")
        headers = self.admin_headers()
        response = self.client.post(
            "/admin/api/releases/import",
            json={"manifest": manifest, "manifest_url": manifest_url},
            headers=headers,
        )
        self.assertEqual(response.status_code, 201, response.json)
        repeated = self.client.post(
            "/admin/api/releases/import",
            json={"manifest": manifest, "manifest_url": manifest_url},
            headers=headers,
        )
        self.assertEqual(repeated.json["status"], "unchanged")
        changed = dict(manifest, firmware_url=manifest["firmware_url"].replace(".bin", "-other.bin"))
        changed_signature = subprocess.run(
            ["openssl", "dgst", "-sha256", "-sign", private],
            input=canonical_payload(changed),
            stdout=subprocess.PIPE,
            check=True,
        ).stdout
        changed["signature"] = base64.b64encode(changed_signature).decode("ascii")
        conflict = self.client.post(
            "/admin/api/releases/import",
            json={"manifest": changed, "manifest_url": manifest_url},
            headers=headers,
        )
        self.assertEqual(conflict.status_code, 409)


if __name__ == "__main__":
    unittest.main()
