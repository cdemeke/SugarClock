"""Deployment authorization is independent of publishing signed release metadata."""

import hashlib
import json
import math
import re
import uuid
from .util import ApiError, json_text, now_epoch

CAPABILITY = "fleet_rollout_v1"
HARDWARE = "ulanzi-tc001-esp32-4mb"
TERMINAL = {"boot_validated", "failed", "rolled_back"}


def capable(device):
    return CAPABILITY in json.loads(device["capabilities_json"])


def version(value):
    return tuple(int(p) for p in value.split("."))


def compatible(device):
    return (
        device["retired_at"] is None
        and device["blocked_at"] is None
        and capable(device)
        and device["hardware"] == HARDWARE
    )


def serialize(connection, row):
    value = dict(row)
    release = connection.execute(
        "SELECT * FROM releases WHERE id=?", (row["release_id"],)
    ).fetchone()
    value["release"] = dict(release)
    value["version"] = release["version"]
    value["release_version"] = release["version"]
    value["paused"] = bool(row["paused"])
    value["cohort_count"] = connection.execute(
        "SELECT COUNT(*) FROM rollout_cohort WHERE rollout_id=?", (row["id"],)
    ).fetchone()[0]
    targets = connection.execute(
        "SELECT t.*,d.last_checkin_at,d.friendly_name FROM rollout_targets t LEFT JOIN devices d USING(installation_id) WHERE rollout_id=? ORDER BY t.updated_at DESC",
        (row["id"],),
    ).fetchall()
    value["target_count"] = len(targets)
    value["outcomes"] = {
        k: 0
        for k in (
            "targeted",
            "waiting_for_contact",
            "deferred",
            "installing",
            "boot_validated",
            "failed",
            "rolled_back",
        )
    }
    for target in targets:
        value["outcomes"][target["status"]] += 1
        if target["status"] == "targeted" and not target["offered_at"]:
            value["outcomes"]["waiting_for_contact"] += 1
    value["targets"] = [dict(t) for t in targets]
    value["rounding"] = "ceil(percentage × frozen cohort / 100)"
    return value


def add_target(connection, rollout_id, installation_id, now):
    connection.execute(
        "INSERT OR IGNORE INTO rollout_targets(id,rollout_id,installation_id,updated_at) VALUES(?,?,?,?)",
        (str(uuid.uuid4()), rollout_id, installation_id, now),
    )


def expand(connection, row):
    cohort = connection.execute(
        "SELECT installation_id FROM rollout_cohort WHERE rollout_id=? ORDER BY rank",
        (row["id"],),
    ).fetchall()
    for item in cohort[: math.ceil(len(cohort) * row["percentage"] / 100)]:
        add_target(connection, row["id"], item["installation_id"], now_epoch())


def create(connection, value, administrator):
    release_id = value.get("release_id")
    kind = value.get("kind")
    if type(release_id) is not int or kind not in {"candidate", "stable"}:
        raise ApiError(
            "invalid_rollout", "release_id and candidate/stable kind required"
        )
    release = connection.execute(
        "SELECT * FROM releases WHERE id=?", (release_id,)
    ).fetchone()
    if not release:
        raise ApiError("release_not_found", "signed imported release required", 404)
    if not release["metadata_complete"]:
        raise ApiError(
            "release_sync_required",
            "Synchronize this existing signed release to load its compatibility metadata",
            422,
        )
    percentage = value.get("percentage", 100)
    if type(percentage) is not int or not 1 <= percentage <= 100:
        raise ApiError("invalid_percentage", "percentage must be 1-100")
    if kind == "stable" and release["channel"] != "stable":
        raise ApiError(
            "stable_manifest_required",
            "Stable rollout requires a signed stable-channel manifest",
            422,
        )
    connection.execute("BEGIN IMMEDIATE")
    previous = connection.execute(
        "SELECT ro.*,r.version FROM rollouts ro JOIN releases r ON r.id=ro.release_id WHERE kind='stable' ORDER BY ro.id DESC LIMIT 1"
    ).fetchone()
    if kind == "stable" and previous:
        if version(release["version"]) <= version(previous["version"]):
            raise ApiError(
                "newer_release_required",
                "Corrective releases must have a strictly higher version",
                409,
            )
    fallback = previous
    while fallback and (fallback["percentage"] != 100 or fallback["paused"]):
        fallback = connection.execute(
            "SELECT * FROM rollouts WHERE id=?", (fallback["previous_stable_id"],)
        ).fetchone()
    now = now_epoch()
    eligible = [
        d
        for d in connection.execute("SELECT * FROM devices")
        if compatible(d)
        and d["hardware"] == release["hardware"]
        and version(d["firmware_version"]) >= version(release["minimum_ota_version"])
    ]
    if kind == "candidate":
        ids = value.get("installation_ids")
        if (
            not isinstance(ids, list)
            or not ids
            or len(ids) > 10000
            or any(not isinstance(i, str) for i in ids)
            or len(set(ids)) != len(ids)
        ):
            raise ApiError("invalid_targets", "select unique installation IDs")
        by_id = {d["installation_id"]: d for d in eligible}
        if any(i not in by_id for i in ids):
            raise ApiError(
                "ineligible_target",
                "Each candidate target must be a compatible, active fleet-aware installation",
                422,
            )
        selected = [by_id[i] for i in ids]
        if any(
            version(release["version"]) <= version(d["firmware_version"])
            for d in selected
        ):
            raise ApiError(
                "newer_release_required",
                "Candidate must be newer than every selected clock",
                409,
            )
        percentage = 100
    else:
        selected = [
            d
            for d in eligible
            if d["last_checkin_at"]
            and d["last_checkin_at"] >= now - 30 * 86400
            and version(release["version"]) > version(d["firmware_version"])
        ]
    cursor = connection.execute(
        "INSERT INTO rollouts(release_id,kind,percentage,previous_stable_id,created_at,administrator) VALUES(?,?,?,?,?,?)",
        (
            release_id,
            kind,
            percentage,
            fallback["id"] if kind == "stable" and fallback else None,
            now,
            administrator,
        ),
    )
    rollout_id = cursor.lastrowid
    selected.sort(
        key=lambda d: hashlib.sha256(
            f"{rollout_id}:{d['installation_id']}".encode()
        ).digest()
    )
    connection.executemany(
        "INSERT INTO rollout_cohort VALUES(?,?,?)",
        [(rollout_id, d["installation_id"], rank) for rank, d in enumerate(selected)],
    )
    row = connection.execute(
        "SELECT * FROM rollouts WHERE id=?", (rollout_id,)
    ).fetchone()
    expand(connection, row)
    audit(
        connection,
        administrator,
        "create_rollout",
        {"rollout_id": rollout_id, "kind": kind, "percentage": percentage},
    )
    connection.commit()
    return serialize(connection, row)


def audit(connection, administrator, action, summary):
    connection.execute(
        "INSERT INTO audit_events(administrator,action,summary_json,created_at,result) VALUES(?,?,?,?, 'succeeded')",
        (administrator, action, json_text(summary), now_epoch()),
    )


def modify(connection, rollout_id, value, administrator):
    if not value or set(value) - {"percentage", "paused"}:
        raise ApiError("invalid_rollout_patch", "Only percentage and paused may change")
    connection.execute("BEGIN IMMEDIATE")
    row = connection.execute(
        "SELECT * FROM rollouts WHERE id=?", (rollout_id,)
    ).fetchone()
    if not row:
        raise ApiError("rollout_not_found", "rollout not found", 404)
    if row["ended_at"] is not None:
        raise ApiError(
            "candidate_finished", "Create a new candidate selection to test again", 409
        )
    percent = value.get("percentage", row["percentage"])
    paused = value.get("paused", bool(row["paused"]))
    if (
        type(percent) is not int
        or not row["percentage"] <= percent <= 100
        or type(paused) is not bool
    ):
        raise ApiError(
            "invalid_rollout_patch",
            "Percentage may only increase, and paused must be boolean",
        )
    if row["kind"] == "candidate" and percent != 100:
        raise ApiError("invalid_percentage", "Candidates use explicit targets")
    connection.execute(
        "UPDATE rollouts SET percentage=?,paused=? WHERE id=?",
        (percent, int(paused), rollout_id),
    )
    row = connection.execute(
        "SELECT * FROM rollouts WHERE id=?", (rollout_id,)
    ).fetchone()
    expand(connection, row)
    audit(
        connection, administrator, "update_rollout", {"rollout_id": rollout_id, **value}
    )
    connection.commit()
    return serialize(connection, row)


def selected_target(connection, device):
    if not compatible(device):
        return None
    # A newer stable rollout supersedes older candidate assignments; otherwise an
    # explicit candidate assignment overrides the stable track for that clock.
    latest_stable = connection.execute(
        "SELECT * FROM rollouts WHERE kind='stable' ORDER BY id DESC LIMIT 1"
    ).fetchone()
    candidate = connection.execute(
        "SELECT ro.* FROM rollouts ro JOIN rollout_targets t ON t.rollout_id=ro.id WHERE ro.kind='candidate' AND t.installation_id=? ORDER BY ro.id DESC LIMIT 1",
        (device["installation_id"],),
    ).fetchone()
    # Ending the newest assignment must not reactivate an older candidate.
    if candidate and candidate["ended_at"] is not None:
        candidate = None
    row = (
        candidate
        if candidate and (not latest_stable or candidate["id"] > latest_stable["id"])
        else latest_stable
    )
    if row is None:
        return None
    target = connection.execute(
        "SELECT * FROM rollout_targets WHERE rollout_id=? AND installation_id=?",
        (row["id"], device["installation_id"]),
    ).fetchone()
    if not target and row["kind"] == "stable":
        if row["percentage"] != 100:
            row = connection.execute(
                "SELECT * FROM rollouts WHERE id=?", (row["previous_stable_id"],)
            ).fetchone()
        if row is None:
            return None
        release = connection.execute(
            "SELECT * FROM releases WHERE id=?", (row["release_id"],)
        ).fetchone()
        if (
            version(release["version"]) <= version(device["firmware_version"])
            or version(device["firmware_version"])
            < version(release["minimum_ota_version"])
            or device["hardware"] != release["hardware"]
        ):
            return None
        add_target(connection, row["id"], device["installation_id"], now_epoch())
        target = connection.execute(
            "SELECT * FROM rollout_targets WHERE rollout_id=? AND installation_id=?",
            (row["id"], device["installation_id"]),
        ).fetchone()
    return (row, target) if target else None


def offer(connection, device):
    selection = selected_target(connection, device)
    if not selection:
        return None
    row, target = selection
    if row["paused"] or target["status"] in TERMINAL:
        return None
    release = connection.execute(
        "SELECT * FROM releases WHERE id=?", (row["release_id"],)
    ).fetchone()
    if (
        version(release["version"]) <= version(device["firmware_version"])
        or version(device["firmware_version"]) < version(release["minimum_ota_version"])
        or device["hardware"] != release["hardware"]
    ):
        return None
    connection.execute(
        "UPDATE rollout_targets SET offered_at=COALESCE(offered_at,?) WHERE id=?",
        (now_epoch(), target["id"]),
    )
    return {
        "target_id": target["id"],
        "rollout_id": row["id"],
        "manifest_url": release["manifest_url"],
        "version": release["version"],
        "channel": release["channel"],
        "sha256": release["firmware_sha256"],
    }


def authorize(connection, device, target_id):
    # Serialize pause and authorization: pause prevents any subsequent starts.
    connection.execute("BEGIN IMMEDIATE")
    device = connection.execute(
        "SELECT * FROM devices WHERE id=?", (device["id"],)
    ).fetchone()
    value = offer(connection, device)
    if not value or value["target_id"] != target_id:
        raise ApiError(
            "update_not_authorized",
            "Update paused, superseded, completed or ineligible",
            409,
        )
    now = now_epoch()
    connection.execute(
        "UPDATE rollout_targets SET authorized_at=?,updated_at=?,status='installing' WHERE id=?",
        (now, now, target_id),
    )
    connection.commit()
    return {
        "authorized": True,
        "expires_at": now + 60,
        "manifest_url": value["manifest_url"],
        "update_offer": value,
    }


def result(connection, device, target_id, value):
    status = value.get("status")
    reason = value.get("reason", value.get("code"))
    if status not in {"deferred", "installing", *TERMINAL} or (
        reason is not None
        and (not isinstance(reason, str) or not re.fullmatch("[a-z0-9_]{1,64}", reason))
    ):
        raise ApiError("invalid_update_result", "Invalid outcome or sanitized reason")
    target = connection.execute(
        "SELECT t.*,r.version FROM rollout_targets t JOIN rollouts ro ON ro.id=t.rollout_id JOIN releases r ON r.id=ro.release_id WHERE t.id=? AND t.installation_id=?",
        (target_id, device["installation_id"]),
    ).fetchone()
    if not target:
        raise ApiError(
            "update_not_found", "Update does not belong to this installation", 404
        )
    if (
        status in {"installing", "boot_validated", "rolled_back"}
        and not target["authorized_at"]
    ):
        raise ApiError(
            "update_not_authorized", "No installation authorization recorded", 409
        )
    if (
        status == "boot_validated"
        and value.get("firmware_version") != target["version"]
    ):
        raise ApiError(
            "invalid_boot_version", "Validated version must match targeted release"
        )
    if target["status"] in TERMINAL:
        if status == target["status"]:
            return {"status": "already_recorded"}
        raise ApiError("update_already_final", "Outcome is already final", 409)
    connection.execute(
        "UPDATE rollout_targets SET status=?,reason=?,updated_at=? WHERE id=?",
        (status, reason, now_epoch(), target_id),
    )
    connection.execute(
        "UPDATE devices SET last_ota_result=?,last_rollback_result=CASE WHEN ?='rolled_back' THEN ? ELSE last_rollback_result END WHERE id=?",
        (status, status, reason or status, device["id"]),
    )
    connection.commit()
    return {"status": "recorded"}


def retry_failed(connection, rollout_id, administrator):
    """A manual retry creates a fresh attempt identity; delayed old results cannot win."""
    connection.execute("BEGIN IMMEDIATE")
    row = connection.execute(
        "SELECT * FROM rollouts WHERE id=?", (rollout_id,)
    ).fetchone()
    if not row:
        raise ApiError("rollout_not_found", "rollout not found", 404)
    if row["ended_at"] is not None:
        raise ApiError(
            "candidate_finished", "Create a new candidate selection to test again", 409
        )
    targets = connection.execute(
        "SELECT id FROM rollout_targets WHERE rollout_id=? AND status='failed'",
        (rollout_id,),
    ).fetchall()
    for target in targets:
        connection.execute(
            "UPDATE rollout_targets SET id=?,status='targeted',reason=NULL,offered_at=NULL,authorized_at=NULL,updated_at=? WHERE id=?",
            (str(uuid.uuid4()), now_epoch(), target["id"]),
        )
    audit(
        connection,
        administrator,
        "retry_failed_rollout",
        {"rollout_id": rollout_id, "retried": len(targets)},
    )
    connection.commit()
    return {"retried": len(targets), "rollout": serialize(connection, row)}


def finish_candidate(connection, rollout_id, administrator):
    """Return selected test installations to stable without forcing a downgrade."""
    connection.execute("BEGIN IMMEDIATE")
    row = connection.execute(
        "SELECT * FROM rollouts WHERE id=?", (rollout_id,)
    ).fetchone()
    if not row:
        raise ApiError("rollout_not_found", "rollout not found", 404)
    if row["kind"] != "candidate":
        raise ApiError(
            "candidate_required", "Only a candidate selection can be finished", 422
        )
    if row["ended_at"] is None:
        connection.execute(
            "UPDATE rollouts SET ended_at=? WHERE id=?", (now_epoch(), rollout_id)
        )
        audit(connection, administrator, "finish_candidate", {"rollout_id": rollout_id})
    connection.commit()
    row = connection.execute(
        "SELECT * FROM rollouts WHERE id=?", (rollout_id,)
    ).fetchone()
    return {"rollout": serialize(connection, row)}
