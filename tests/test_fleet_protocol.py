"""Fixture regression coverage for legacy and additive fleet v1 reporting."""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "fleet"))
from sugarfleet.telemetry import BOOLEAN_FEATURES, validate_telemetry
from sugarfleet.validation import validate_checkin
from sugarfleet.util import ApiError

PROTOCOL = ROOT / "fleet" / "protocol" / "v1"


def fixture(name):
    return json.loads((PROTOCOL / "fixtures" / name).read_text())


class AdditiveProtocolFixtures(unittest.TestCase):
    def test_old_and_new_reporting_are_accepted(self):
        for name in ("check-in-request.json", "fleet-check-in-request.json"):
            with self.subTest(name=name):
                value = fixture(name)
                validate_checkin(value)
                validate_telemetry(value)
        self.assertNotIn("features", fixture("check-in-request.json"))
        self.assertIn("fleet_rollout_v1", fixture("fleet-check-in-request.json")["capabilities"])

    def test_snapshot_fixture_rejects_private_and_untyped_fields(self):
        for key, value in (("weather_city", "Private place"), ("glucose", 120),
                           ("weather_enabled", "true"), ("companion_character", True),
                           ("data_source", "https://private.example")):
            body = fixture("fleet-check-in-request.json")
            body["features"][key] = value
            with self.subTest(key=key), self.assertRaises(ApiError):
                validate_telemetry(body)

    def test_documented_feature_types_match_server_allowlist(self):
        schema = json.loads((PROTOCOL / "protocol.schema.json").read_text())
        feature_schema = schema["$defs"]["features"]
        self.assertFalse(feature_schema["additionalProperties"])
        boolean_keys = {key for key, definition in feature_schema["properties"].items()
                        if definition.get("type") == "boolean"}
        self.assertEqual(boolean_keys, BOOLEAN_FEATURES)
        for source in feature_schema["properties"]["data_source"]["enum"]:
            validate_telemetry({"features": {"schema_version": 1, "data_source": source}})

    def test_offer_and_authorization_examples_bind_same_release(self):
        offered = fixture("check-in-response.json")["update_offer"]
        authorized = fixture("update-authorization-response.json")
        self.assertEqual(offered, authorized["update_offer"])
        self.assertEqual(offered["manifest_url"], authorized["manifest_url"])
        self.assertGreater(authorized["expires_at"], fixture("check-in-response.json")["server_time"])
        self.assertIsNone(fixture("check-in-no-offer-response.json")["update_offer"])
        completed = next(v for v in fixture("update-results.json") if v["status"] == "boot_validated")
        self.assertEqual(completed["firmware_version"], offered["version"])


if __name__ == "__main__":
    unittest.main()
