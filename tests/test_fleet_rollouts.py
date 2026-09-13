"""End-to-end targeting, telemetry, migration and abuse regression coverage."""

import json
import os
import tempfile
import unittest
import uuid
from fleet.sugarfleet import create_app
from fleet.sugarfleet.db import get_db, migrate
from fleet.sugarfleet.security import hash_device_credential
from fleet.sugarfleet.util import now_epoch

CREDENTIAL = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"


class RolloutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.app = create_app(
            {
                "TESTING": True,
                "DATABASE": os.path.join(self.temp.name, "fleet.db"),
                "SECRET_KEY": "s" * 32,
                "GITHUB_ALLOWLIST": ["admin"],
                "SESSION_COOKIE_SECURE": False,
            }
        )
        self.client = self.app.test_client()
        with self.client.session_transaction() as session:
            session["github_login"] = "admin"
            session["csrf_token"] = "test"
        self.admin = {"X-CSRF-Token": "test"}
        self.auth = {"Authorization": "Bearer " + CREDENTIAL}

    def tearDown(self):
        self.temp.cleanup()

    def clock(self, capable=True, age=0):
        installation = str(uuid.uuid4())
        now = now_epoch()
        with self.app.app_context():
            db = get_db()
            db.execute(
                "INSERT INTO devices(installation_id,credential_hash,hardware,management_protocol,first_seen,last_seen,last_checkin_at,firmware_version,timezone,capabilities_json) VALUES(?,?,?,1,?,?,?,'1.0.0','UTC',?)",
                (
                    installation,
                    hash_device_credential(CREDENTIAL, "s" * 32),
                    "ulanzi-tc001-esp32-4mb",
                    now,
                    now - age,
                    now - age,
                    json.dumps(["fleet_rollout_v1"] if capable else []),
                ),
            )
            db.commit()
        return installation

    def release(self, version="1.1.0", channel="stable"):
        with self.app.app_context():
            db = get_db()
            r = db.execute(
                "INSERT INTO releases(version,channel,manifest_url,firmware_url,firmware_sha256,firmware_size,published_at,imported_at,approved_by) VALUES(?,?,?,?,?,1,'2026-09-12T00:00:00Z',1,'admin')",
                (
                    version,
                    channel,
                    f"https://example.com/releases/{version}/{channel}.json",
                    f"https://example.com/{version}.bin",
                    "a" * 64,
                ),
            )
            db.commit()
            return r.lastrowid

    def create(self, release, kind="stable", percentage=10, ids=None):
        return self.client.post(
            "/admin/api/rollouts",
            json={
                "release_id": release,
                "kind": kind,
                "percentage": percentage,
                "installation_ids": ids,
            },
            headers=self.admin,
        )

    def check(self, installation, features=None, capable=True):
        value = {
            "installation_id": installation,
            "firmware_version": "1.0.0",
            "channel": "stable",
            "uptime_seconds": 30,
            "capabilities": ["fleet_rollout_v1"] if capable else [],
        }
        if features is not None:
            value["features"] = features
        return self.client.post("/device/v1/check-in", json=value, headers=self.auth)

    def test_frozen_cohort_expansion_and_legacy_exclusion(self):
        ids = [self.clock() for _ in range(20)]
        legacy = self.clock(False)
        dormant = self.clock(age=31 * 86400)
        release = self.release()
        created = self.create(release).json["rollout"]
        self.assertEqual(created["cohort_count"], 20)
        self.assertEqual(created["target_count"], 2)
        chosen = {t["installation_id"] for t in created["targets"]}
        for installation in ids:
            for _ in range(2):
                self.assertEqual(
                    bool(self.check(installation).json["update_offer"]),
                    installation in chosen,
                )
        new = self.clock()
        self.assertIsNone(self.check(new).json["update_offer"])
        self.assertIsNone(self.check(legacy, capable=False).json["update_offer"])
        expanded = self.client.patch(
            f"/admin/api/rollouts/{created['id']}",
            json={"percentage": 100},
            headers=self.admin,
        )
        self.assertEqual(expanded.status_code, 200)
        self.assertTrue(
            chosen.issubset(
                {t["installation_id"] for t in expanded.json["rollout"]["targets"]}
            )
        )
        self.assertIsNotNone(self.check(new).json["update_offer"])
        self.assertIsNotNone(self.check(dormant).json["update_offer"])
        self.assertIsNone(self.check(legacy, capable=False).json["update_offer"])

    def test_candidate_ids_nickname_changes_pause_and_outcome(self):
        chosen = self.clock()
        other = self.clock()
        release = self.release(channel="preview")
        self.assertIsNone(
            self.check(chosen).json["update_offer"]
        )  # import never deploys
        created = self.create(release, "candidate", ids=[chosen]).json["rollout"]
        self.client.patch(
            "/admin/api/devices/1", json={"friendly_name": "Same"}, headers=self.admin
        )
        self.client.patch(
            "/admin/api/devices/2", json={"friendly_name": "Same"}, headers=self.admin
        )
        offer = self.check(chosen).json["update_offer"]
        self.assertIsNone(self.check(other).json["update_offer"])
        path = f"/device/v1/updates/{offer['target_id']}"
        self.client.patch(
            f"/admin/api/rollouts/{created['id']}",
            json={"paused": True},
            headers=self.admin,
        )
        self.assertEqual(
            self.client.post(
                path + "/authorize", json={"installation_id": chosen}, headers=self.auth
            ).status_code,
            409,
        )
        self.client.patch(
            f"/admin/api/rollouts/{created['id']}",
            json={"paused": False},
            headers=self.admin,
        )
        self.assertEqual(
            self.client.post(
                path + "/authorize", json={"installation_id": other}, headers=self.auth
            ).status_code,
            409,
        )
        self.assertEqual(
            self.client.post(
                path + "/authorize", json={"installation_id": chosen}, headers=self.auth
            ).status_code,
            200,
        )
        value = {
            "installation_id": chosen,
            "status": "boot_validated",
            "firmware_version": "1.0.0",
        }
        self.assertEqual(
            self.client.post(
                path + "/result", json=value, headers=self.auth
            ).status_code,
            400,
        )
        value["firmware_version"] = "1.1.0"
        self.assertEqual(
            self.client.post(path + "/result", json=value, headers=self.auth).json[
                "status"
            ],
            "recorded",
        )
        self.assertEqual(
            self.client.post(path + "/result", json=value, headers=self.auth).json[
                "status"
            ],
            "already_recorded",
        )
        self.assertIsNone(self.check(chosen).json["update_offer"])

    def test_previous_stable_fallback_and_no_downgrade_promotion(self):
        ids = [self.clock() for _ in range(10)]
        first = self.create(self.release(), percentage=100).json["rollout"]
        second = self.create(self.release("1.2.0")).json["rollout"]
        chosen = second["targets"][0]["installation_id"]
        for installation in ids:
            offer = self.check(installation).json["update_offer"]
            self.assertEqual(
                offer["version"], "1.2.0" if installation == chosen else "1.1.0"
            )
        self.assertEqual(self.create(self.release("1.0.1")).status_code, 409)
        self.assertEqual(
            self.client.patch(
                f"/admin/api/rollouts/{second['id']}",
                json={"percentage": 1},
                headers=self.admin,
            ).status_code,
            400,
        )

    def test_telemetry_unknown_values_and_no_secret_fields(self):
        a = self.clock()
        b = self.clock(False)
        self.assertEqual(
            self.check(
                a,
                {"schema_version": 1, "data_source": "dexcom", "weather_enabled": True},
            ).status_code,
            200,
        )
        self.check(b, capable=False)
        overview = self.client.get("/admin/api/overview").json
        self.assertEqual(
            overview["feature_counts"]["weather_enabled"],
            {"enabled": 1, "known": 1, "unknown": 1},
        )
        self.assertEqual(overview["source_counts"], {"dexcom": 1, "unknown": 1})
        self.assertEqual(overview["feature_denominator"], 2)
        for bad in (
            {"schema_version": 1, "weather_city": "Boston"},
            {"schema_version": 1, "weather_enabled": 1},
            {"schema_version": 2},
            {"schema_version": 1, "data_source": "secret@email.com"},
        ):
            self.assertEqual(self.check(a, bad).status_code, 400)
        self.assertEqual(len(overview["daily"]), 1)

    def test_capacity_cleanup_block_tombstone_and_reporting(self):
        self.app.config["ENROLLMENT_MAX_DEVICES"] = 2
        active = self.clock()
        blocked = self.clock()
        self.assertEqual(
            self.client.post(
                "/admin/api/devices/2/state",
                json={"state": "blocked"},
                headers=self.admin,
            ).status_code,
            200,
        )
        with self.app.app_context():
            db = get_db()
            db.execute(
                "UPDATE devices SET last_checkin_at=NULL,first_seen=? WHERE installation_id=?",
                (now_epoch() - 90000, blocked),
            )
            db.commit()
        response = self.client.post("/admin/api/cleanup", json={}, headers=self.admin)
        self.assertEqual(response.json["deleted"], 0)
        with self.app.app_context():
            db = get_db()
            db.execute("DELETE FROM devices WHERE installation_id=?", (blocked,))
            db.commit()  # tombstone also protects manual abuse cleanup

        def register(installation):
            return self.client.post(
                "/device/v1/register",
                json={
                    "installation_id": installation,
                    "hardware": "ulanzi-tc001-esp32-4mb",
                    "management_protocol": 1,
                    "firmware_version": "1.0.0",
                    "timezone": "UTC",
                },
                headers=self.auth,
            )

        self.assertEqual(register(blocked).status_code, 403)
        self.assertEqual(register(str(uuid.uuid4())).status_code, 201)
        self.assertEqual(register(str(uuid.uuid4())).status_code, 503)
        self.assertEqual(self.check(active).status_code, 200)
        self.assertTrue(
            self.client.get("/admin/api/overview").json["capacity"]["warning"]
        )

    def test_shared_global_limiter_without_blocking_authenticated_clocks(self):
        active = self.clock()
        self.app.config["ENROLLMENT_GLOBAL_RATE_LIMIT"] = 3
        for i in range(5):
            response = self.client.post(
                "/device/v1/register",
                json={
                    "installation_id": str(uuid.uuid4()),
                    "hardware": "ulanzi-tc001-esp32-4mb",
                    "management_protocol": 1,
                    "firmware_version": "1.0.0",
                    "timezone": "UTC",
                },
                headers=self.auth,
                environ_overrides={"REMOTE_ADDR": f"10.0.0.{i}"},
            )
            self.assertEqual(response.status_code, 201 if i < 3 else 429)
        self.assertEqual(self.check(active).status_code, 200)
        with self.app.app_context():
            self.assertEqual(
                get_db()
                .execute("SELECT COUNT(*) FROM registration_limits")
                .fetchone()[0],
                4,
            )

    def test_csrf_and_candidate_legacy_target_rejection(self):
        legacy = self.clock(False)
        release = self.release()
        self.assertEqual(
            self.create(release, "candidate", ids=[legacy]).status_code, 422
        )
        self.assertEqual(
            self.client.post(
                "/admin/api/rollouts", json={"release_id": release, "kind": "stable"}
            ).status_code,
            403,
        )

    def test_corrective_supersedes_bad_partial_without_exposing_it(self):
        ids = [self.clock() for _ in range(20)]
        first = self.create(self.release(), percentage=100).json["rollout"]
        bad = self.create(self.release("1.2.0")).json["rollout"]
        self.client.patch(
            f"/admin/api/rollouts/{bad['id']}",
            json={"paused": True},
            headers=self.admin,
        )
        correction = self.create(self.release("1.3.0")).json["rollout"]
        self.assertEqual(correction["previous_stable_id"], first["id"])
        targets = {t["installation_id"] for t in correction["targets"]}
        for installation in ids:
            self.assertEqual(
                self.check(installation).json["update_offer"]["version"],
                "1.3.0" if installation in targets else "1.1.0",
            )

    def test_retry_creates_new_attempt_and_requires_fresh_authorization(self):
        clock = self.clock()
        rollout = self.create(self.release(), percentage=100).json["rollout"]
        offer = self.check(clock).json["update_offer"]
        path = "/device/v1/updates/" + offer["target_id"]
        self.client.post(
            path + "/authorize", json={"installation_id": clock}, headers=self.auth
        )
        self.client.post(
            path + "/result",
            json={
                "installation_id": clock,
                "status": "failed",
                "reason": "update_failed",
            },
            headers=self.auth,
        )
        self.check(clock)
        self.assertEqual(
            self.client.get("/admin/api/devices/1").json["device"]["last_ota_result"],
            "failed",
        )
        self.client.patch(
            f"/admin/api/rollouts/{rollout['id']}",
            json={"paused": True},
            headers=self.admin,
        )
        retry = self.client.post(
            f"/admin/api/rollouts/{rollout['id']}/retry", headers=self.admin
        )
        self.assertEqual(retry.json["retried"], 1)
        self.assertTrue(retry.json["rollout"]["paused"])
        self.assertIsNone(self.check(clock).json["update_offer"])
        self.assertEqual(
            self.client.post(
                path + "/result",
                json={"installation_id": clock, "status": "failed"},
                headers=self.auth,
            ).status_code,
            404,
        )
        self.client.patch(
            f"/admin/api/rollouts/{rollout['id']}",
            json={"paused": False},
            headers=self.admin,
        )
        new_offer = self.check(clock).json["update_offer"]
        self.assertNotEqual(new_offer["target_id"], offer["target_id"])
        self.assertEqual(
            self.client.post(
                "/device/v1/updates/" + new_offer["target_id"] + "/result",
                json={
                    "installation_id": clock,
                    "status": "boot_validated",
                    "firmware_version": "1.1.0",
                },
                headers=self.auth,
            ).status_code,
            409,
        )

    def test_minimum_version_excludes_dynamic_and_frozen_targets(self):
        clock = self.clock()
        release = self.release()
        with self.app.app_context():
            db = get_db()
            db.execute(
                "UPDATE releases SET minimum_ota_version='1.0.1' WHERE id=?", (release,)
            )
            db.commit()
        rollout = self.create(release, percentage=100).json["rollout"]
        self.assertEqual(rollout["target_count"], 0)
        self.assertIsNone(self.check(clock).json["update_offer"])
        self.assertEqual(
            self.client.get("/admin/api/rollouts").json["rollouts"][0]["target_count"],
            0,
        )

    def test_registration_cleanup_recovers_ceiling_and_preserves_retired(self):
        active = self.clock()
        retired = self.clock()
        stale = self.clock()
        self.client.post(
            "/admin/api/devices/2/state", json={"state": "retired"}, headers=self.admin
        )
        with self.app.app_context():
            db = get_db()
            db.execute(
                "UPDATE devices SET last_checkin_at=NULL,first_seen=? WHERE installation_id IN (?,?)",
                (now_epoch() - 90000, retired, stale),
            )
            db.commit()
        self.assertEqual(
            self.client.post("/admin/api/cleanup", headers=self.admin).json["deleted"],
            1,
        )
        self.assertEqual(self.check(retired).status_code, 403)
        self.assertEqual(self.check(active).status_code, 200)

    def test_v5_migration_preserves_retired_and_enables_pending(self):
        self._assert_migrated_pending_survives_cleanup("0006")

    def test_v6_upgrade_preserves_existing_registrations_without_fake_activity(self):
        self._assert_migrated_pending_survives_cleanup("0007")

    def _assert_migrated_pending_survives_cleanup(self, stop_before):
        import pathlib
        import sqlite3

        path = os.path.join(self.temp.name, "old.db")
        db = sqlite3.connect(path)
        db.execute(
            "CREATE TABLE schema_migrations(version TEXT PRIMARY KEY,applied_at INTEGER NOT NULL)"
        )
        migrations = (
            pathlib.Path(__file__).resolve().parents[1] / "fleet/sugarfleet/migrations"
        )
        for migration in sorted(migrations.glob("*.sql")):
            if migration.name.startswith(stop_before):
                break
            db.executescript(migration.read_text())
            db.execute("INSERT INTO schema_migrations VALUES(?,1)", (migration.name,))
            db.commit()
        identities = [str(uuid.uuid4()), str(uuid.uuid4())]
        original_hash = hash_device_credential(CREDENTIAL, "s" * 32)
        for identity, retired in zip(identities, (None, 100)):
            db.execute(
                "INSERT INTO devices(installation_id,credential_hash,hardware,management_protocol,first_seen,last_seen,firmware_version,timezone,retired_at,friendly_name) VALUES(?,?,'ulanzi-tc001-esp32-4mb',1,1,1,'1.0.0','UTC',?,'My desk')",
                (identity, original_hash, retired),
            )
        if stop_before == "0007":
            db.execute(
                "UPDATE devices SET verification_state='verified' WHERE retired_at IS NULL"
            )
        db.commit()
        db.close()
        app = create_app({"TESTING": True, "DATABASE": path, "SECRET_KEY": "s" * 32})
        with app.app_context():
            rows = get_db().execute("SELECT * FROM devices ORDER BY id").fetchall()
            self.assertEqual(rows[0]["verification_state"], "verified")
            self.assertEqual(rows[1]["retired_at"], 100)
            self.assertEqual(rows[0]["credential_hash"], original_hash)
            self.assertEqual(rows[0]["friendly_name"], "My desk")
            self.assertIsNone(rows[0]["last_checkin_at"])
            self.assertEqual(rows[0]["registration_cleanup_exempt"], 1)

        # Another clock checks in first and triggers hourly cleanup while the old
        # pending clock is still in its one-hour legacy retry backoff.
        client = app.test_client()
        fresh = str(uuid.uuid4())
        registered = client.post(
            "/device/v1/register",
            headers=self.auth,
            json={
                "installation_id": fresh,
                "hardware": "ulanzi-tc001-esp32-4mb",
                "firmware_version": "1.0.0",
                "timezone": "UTC",
                "management_protocol": 1,
            },
        )
        self.assertEqual(registered.status_code, 201)
        checkin = {
            "installation_id": fresh,
            "firmware_version": "1.0.0",
            "channel": "stable",
            "uptime_seconds": 30,
        }
        self.assertEqual(
            client.post(
                "/device/v1/check-in", headers=self.auth, json=checkin
            ).status_code,
            200,
        )
        with app.app_context():
            from fleet.sugarfleet.telemetry import overview

            db = get_db()
            pending = db.execute(
                "SELECT * FROM devices WHERE installation_id=?", (identities[0],)
            ).fetchone()
            self.assertIsNotNone(pending)
            self.assertEqual(pending["credential_hash"], original_hash)
            self.assertEqual(pending["friendly_name"], "My desk")
            self.assertIsNone(pending["last_checkin_at"])
            self.assertEqual(overview(db)["counts"]["active_30d"], 1)
        checkin["installation_id"] = identities[0]
        self.assertEqual(
            client.post(
                "/device/v1/check-in", headers=self.auth, json=checkin
            ).status_code,
            200,
        )
        checkin["installation_id"] = identities[1]
        self.assertEqual(
            client.post(
                "/device/v1/check-in", headers=self.auth, json=checkin
            ).status_code,
            403,
        )

    def test_finish_candidate_returns_stable_and_revokes_stale_offer(self):
        clock = self.clock()
        stable = self.create(self.release(), percentage=100).json["rollout"]
        older = self.create(
            self.release("1.2.0", "preview"), "candidate", ids=[clock]
        ).json["rollout"]
        candidate = self.create(
            self.release("1.3.0", "preview"), "candidate", ids=[clock]
        ).json["rollout"]
        offer = self.check(clock).json["update_offer"]
        response = self.client.post(
            f"/admin/api/rollouts/{candidate['id']}/finish-candidate",
            headers=self.admin,
        )
        self.assertEqual(response.status_code, 200)
        self.assertIsNotNone(response.json["rollout"]["ended_at"])
        self.assertEqual(len(response.json["rollout"]["targets"]), 1)
        self.assertEqual(self.check(clock).json["update_offer"]["version"], "1.1.0")
        self.assertEqual(
            self.client.post(
                "/device/v1/updates/" + offer["target_id"] + "/authorize",
                json={"installation_id": clock},
                headers=self.auth,
            ).status_code,
            409,
        )
        self.assertEqual(
            self.client.post(
                f"/admin/api/rollouts/{stable['id']}/finish-candidate",
                headers=self.admin,
            ).status_code,
            422,
        )
        self.assertEqual(
            self.client.patch(
                f"/admin/api/rollouts/{candidate['id']}",
                json={"paused": False},
                headers=self.admin,
            ).status_code,
            409,
        )
        # A newer already-installed candidate waits for a newer stable, never downgrades.
        with self.app.app_context():
            db = get_db()
            db.execute(
                "UPDATE devices SET firmware_version='1.3.0' WHERE installation_id=?",
                (clock,),
            )
            db.commit()
            from fleet.sugarfleet.rollouts import offer as get_offer

            self.assertIsNone(
                get_offer(
                    db,
                    db.execute(
                        "SELECT * FROM devices WHERE installation_id=?", (clock,)
                    ).fetchone(),
                )
            )

    def test_registration_race_rechecks_identity_under_write_lock(self):
        from unittest import mock

        installation = str(uuid.uuid4())
        body = {
            "installation_id": installation,
            "hardware": "ulanzi-tc001-esp32-4mb",
            "management_protocol": 1,
            "firmware_version": "1.0.0",
            "timezone": "UTC",
        }

        def intervening_registration(*args):
            # Simulate another worker committing after this request's first read.
            with self.app.app_context():
                db = get_db()
                db.execute(
                    "INSERT INTO devices(installation_id,credential_hash,hardware,management_protocol,first_seen,last_seen,firmware_version,timezone) VALUES(?,?,?,1,1,1,'1.0.0','UTC')",
                    (
                        installation,
                        hash_device_credential(CREDENTIAL, "s" * 32),
                        body["hardware"],
                    ),
                )
                db.commit()
            return True

        with mock.patch(
            "fleet.sugarfleet.device.registration_rate_allowed",
            side_effect=intervening_registration,
        ):
            response = self.client.post(
                "/device/v1/register", json=body, headers=self.auth
            )
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json["status"], "already_registered")
