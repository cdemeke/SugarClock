"""Private administrator UI and JSON API."""

import json
import uuid

from flask import (
    Blueprint,
    current_app,
    redirect,
    render_template,
    request,
    session,
    url_for,
)

from .auth import admin_required, csrf_required
from .db import get_db
from . import telemetry, rollouts
from .geolocation import location_label as format_detected_location
from .releases import import_manifest, synchronize
from .util import (
    ApiError,
    connectivity_state,
    error_response,
    json_body,
    json_text,
    now_epoch,
)
from .validation import validate_command, validate_expiration

bp = Blueprint("admin", __name__, url_prefix="/admin")


@bp.errorhandler(ApiError)
def handle_api_error(error):
    return error_response(error)


def _device_json(row, *, detail=False):
    detected_label = format_detected_location(
        row["detected_city"], row["detected_region"], row["detected_country_code"]
    )
    value = {
        "id": row["id"],
        "installation_id": row["installation_id"],
        "friendly_name": row["friendly_name"],
        "location_label": row["location_label"],
        "detected_location": (
            {
                "label": detected_label,
                "city": row["detected_city"],
                "region": row["detected_region"],
                "country_code": row["detected_country_code"],
                "checked_at": row["detected_location_checked_at"],
            }
            if detected_label
            else None
        ),
        "verification_state": row["verification_state"],
        "connectivity": (
            "blocked"
            if row["blocked_at"]
            else connectivity_state(row["last_seen"], row["retired_at"])
        ),
        "blocked_at": row["blocked_at"],
        "retired_at": row["retired_at"],
        "last_checkin_at": row["last_checkin_at"],
        "rollout_capable": rollouts.capable(row),
        "features": json.loads(row["features_json"] or "null"),
        "features_reported_at": row["features_reported_at"],
        "hardware": row["hardware"],
        "first_seen": row["first_seen"],
        "last_seen": row["last_seen"],
        "firmware_version": row["firmware_version"],
        "channel": row["channel"],
        "timezone": row["timezone"],
        "last_ota_result": row["last_ota_result"],
        "last_rollback_result": row["last_rollback_result"],
    }
    if detail:
        value.update(
            {
                "running_partition": row["running_partition"],
                "boot_partition": row["boot_partition"],
                "previous_partition": row["previous_partition"],
                "previous_partition_available": bool(
                    row["previous_partition_available"]
                ),
                "maintenance_window": json.loads(
                    row["maintenance_window_json"] or "null"
                ),
                "config_revision": row["config_revision"],
                "config_hash": row["config_hash"],
                "uptime_seconds": row["uptime_seconds"],
                "free_heap_bucket": row["free_heap_bucket"],
                "wifi_signal_bucket": row["wifi_signal_bucket"],
                "battery_percent": row["battery_percent"],
                "charging": (
                    bool(row["charging"]) if row["charging"] is not None else None
                ),
                "health_codes": json.loads(row["health_json"]),
            }
        )
    return value


def _command_json(row):
    return {
        "id": row["id"],
        "type": row["type"],
        "payload": json.loads(row["payload_json"]),
        "status": row["status"],
        "created_at": row["created_at"],
        "expires_at": row["expires_at"],
        "delivered_at": row["delivered_at"],
        "acknowledged_at": row["acknowledged_at"],
        "attempt_count": row["attempt_count"],
        "result": json.loads(row["result_json"] or "null"),
        "administrator": row["administrator"],
    }


@bp.get("")
@bp.get("/")
@admin_required
def index():
    return redirect(url_for("admin.overview_page"))


@bp.get("/devices")
@admin_required
def device_list_page():
    connection = get_db()
    devices = [
        _device_json(row)
        for row in connection.execute("SELECT * FROM devices ORDER BY last_seen DESC")
    ]
    return render_template("devices.html", devices=devices)


@bp.get("/devices/<int:device_id>")
@admin_required
def device_detail_page(device_id):
    connection = get_db()
    device = connection.execute(
        "SELECT * FROM devices WHERE id=?", (device_id,)
    ).fetchone()
    if not device:
        return render_template("not_found.html"), 404
    commands = connection.execute(
        "SELECT * FROM commands WHERE device_id=? ORDER BY created_at DESC LIMIT 30",
        (device_id,),
    ).fetchall()
    return render_template(
        "device_detail.html",
        device=_device_json(device, detail=True),
        commands=[_command_json(row) for row in commands],
    )


@bp.get("/releases")
@admin_required
def releases_page():
    releases = (
        get_db()
        .execute("SELECT * FROM releases ORDER BY published_at DESC, channel")
        .fetchall()
    )
    return render_template("releases.html", releases=releases)


@bp.get("/api/devices")
@admin_required
def api_devices():
    state = request.args.get("state")
    channel = request.args.get("channel")
    devices = [
        _device_json(row)
        for row in get_db().execute("SELECT * FROM devices ORDER BY last_seen DESC")
    ]
    if state:
        devices = [device for device in devices if device["connectivity"] == state]
    if channel:
        devices = [device for device in devices if device["channel"] == channel]
    return {"devices": devices}


@bp.get("/api/devices/<int:device_id>")
@admin_required
def api_device(device_id):
    connection = get_db()
    row = connection.execute(
        "SELECT * FROM devices WHERE id=?", (device_id,)
    ).fetchone()
    if not row:
        raise ApiError("device_not_found", "device not found", 404)
    commands = connection.execute(
        "SELECT * FROM commands WHERE device_id=? ORDER BY created_at DESC LIMIT 100",
        (device_id,),
    ).fetchall()
    return {
        "device": _device_json(row, detail=True),
        "commands": [_command_json(item) for item in commands],
    }


@bp.patch("/api/devices/<int:device_id>")
@admin_required
@csrf_required
def update_device(device_id):
    value = json_body()
    allowed = {"friendly_name", "location_label"}
    if not value or not set(value).issubset(allowed):
        raise ApiError(
            "invalid_device_patch",
            "only friendly_name and location_label may be updated directly",
        )

    normalized = {}
    limits = {"friendly_name": 80, "location_label": 120}
    for field, raw in value.items():
        if not isinstance(raw, str) or len(raw.strip()) > limits[field]:
            raise ApiError(
                "invalid_" + field,
                f"{field} must be a string of at most {limits[field]} characters",
            )
        normalized[field] = raw.strip()

    connection = get_db()
    device = connection.execute(
        "SELECT * FROM devices WHERE id=?", (device_id,)
    ).fetchone()
    if not device:
        raise ApiError("device_not_found", "device not found", 404)
    now = now_epoch()
    assignments = ", ".join(f"{field}=?" for field in normalized)
    connection.execute(
        f"UPDATE devices SET {assignments} WHERE id=?",
        (*normalized.values(), device_id),
    )
    changes = {
        field: {"before": device[field], "after": new_value}
        for field, new_value in normalized.items()
        if device[field] != new_value
    }
    connection.execute(
        "INSERT INTO audit_events (administrator, action, target_device_id, summary_json, created_at, result) "
        "VALUES (?, 'update_device_identity', ?, ?, ?, 'succeeded')",
        (
            session["github_login"],
            device_id,
            json_text({"changes": changes}),
            now,
        ),
    )
    connection.commit()
    updated = connection.execute(
        "SELECT * FROM devices WHERE id=?", (device_id,)
    ).fetchone()
    return {"status": "updated", "device": _device_json(updated, detail=True)}


@bp.post("/api/devices/<int:device_id>/commands")
@admin_required
@csrf_required
def queue_command(device_id):
    value = json_body()
    command_type = value.get("type")
    payload = value.get("payload", {})
    validate_command(command_type, payload)
    if command_type in {"ota_install", "ota_rollback_previous", "set_channel"}:
        raise ApiError(
            "use_rollouts",
            "Use release targeting; unmanaged installs and manual rollback are disabled",
            422,
        )
    now = now_epoch()
    expires_at = validate_expiration(value.get("expires_at"), now)
    connection = get_db()
    device = connection.execute(
        "SELECT * FROM devices WHERE id=?", (device_id,)
    ).fetchone()
    if (
        not device
        or device["retired_at"] is not None
        or device["blocked_at"] is not None
    ):
        raise ApiError("device_not_found", "active device not found", 404)
    command_id = str(uuid.uuid4())
    summary = {
        "command_id": command_id,
        "type": command_type,
        "override_window": bool(payload.get("override_window")),
    }
    audit = connection.execute(
        "INSERT INTO audit_events (administrator, action, target_device_id, summary_json, created_at, result) "
        "VALUES (?, 'queue_command', ?, ?, ?, 'queued')",
        (session["github_login"], device_id, json_text(summary), now),
    )
    connection.execute(
        "INSERT INTO commands (id, device_id, type, payload_json, created_at, expires_at, administrator, audit_event_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        (
            command_id,
            device_id,
            command_type,
            json_text(payload),
            now,
            expires_at,
            session["github_login"],
            audit.lastrowid,
        ),
    )
    connection.commit()
    return {
        "command": {"id": command_id, "status": "queued", "expires_at": expires_at}
    }, 201


@bp.get("/api/releases")
@admin_required
def api_releases():
    rows = (
        get_db()
        .execute("SELECT * FROM releases ORDER BY published_at DESC, channel")
        .fetchall()
    )
    return {
        "releases": [
            {
                "id": row["id"],
                "version": row["version"],
                "channel": row["channel"],
                "manifest_url": row["manifest_url"],
                "firmware_url": row["firmware_url"],
                "firmware_sha256": row["firmware_sha256"],
                "firmware_size": row["firmware_size"],
                "published_at": row["published_at"],
                "known_good": bool(row["known_good"]),
                "hardware": row["hardware"],
                "minimum_ota_version": row["minimum_ota_version"],
                "metadata_complete": bool(row["metadata_complete"]),
            }
            for row in rows
        ]
    }


@bp.post("/api/releases/import")
@admin_required
@csrf_required
def api_import_release():
    value = json_body()
    if not isinstance(value.get("manifest"), dict) or not isinstance(
        value.get("manifest_url"), str
    ):
        raise ApiError("invalid_import", "manifest and manifest_url are required")
    connection = get_db()
    release_id, created = import_manifest(
        connection,
        value["manifest"],
        value["manifest_url"],
        session["github_login"],
        current_app.config["OTA_PUBLIC_KEYS_DIR"],
    )
    connection.commit()
    return {
        "release_id": release_id,
        "status": "imported" if created else "unchanged",
    }, (201 if created else 200)


@bp.post("/api/releases/sync")
@admin_required
@csrf_required
def api_sync_releases():
    try:
        result = synchronize(
            get_db(),
            current_app.config["GITHUB_REPOSITORY"],
            session["github_login"],
            current_app.config["OTA_PUBLIC_KEYS_DIR"],
            current_app.config.get("GITHUB_API_TOKEN"),
        )
    except OSError as error:
        raise ApiError(
            "github_unavailable", "GitHub release synchronization failed", 502
        ) from error
    return result


@bp.get("/overview")
@admin_required
def overview_page():
    return render_template("overview.html")


@bp.get("/api/overview")
@admin_required
def api_overview():
    return telemetry.overview(get_db())


@bp.get("/api/rollouts")
@admin_required
def api_rollouts():
    connection = get_db()
    return {
        "rollouts": [
            rollouts.serialize(connection, row)
            for row in connection.execute(
                "SELECT * FROM rollouts ORDER BY id DESC LIMIT 100"
            )
        ]
    }


@bp.post("/api/rollouts")
@admin_required
@csrf_required
def create_rollout():
    return {
        "rollout": rollouts.create(get_db(), json_body(), session["github_login"])
    }, 201


@bp.patch("/api/rollouts/<int:rollout_id>")
@admin_required
@csrf_required
def update_rollout(rollout_id):
    return {
        "rollout": rollouts.modify(
            get_db(), rollout_id, json_body(), session["github_login"]
        )
    }


@bp.post("/api/devices/<int:device_id>/state")
@admin_required
@csrf_required
def set_device_state(device_id):
    state = json_body().get("state")
    if state not in {"active", "retired", "blocked"}:
        raise ApiError("invalid_state", "state must be active, retired or blocked")
    connection = get_db()
    row = connection.execute(
        "SELECT * FROM devices WHERE id=?", (device_id,)
    ).fetchone()
    if not row:
        raise ApiError("device_not_found", "device not found", 404)
    now = now_epoch()
    connection.execute(
        "UPDATE devices SET blocked_at=?,retired_at=? WHERE id=?",
        (
            now if state == "blocked" else None,
            now if state == "retired" else None,
            device_id,
        ),
    )
    if state == "blocked":
        connection.execute(
            "INSERT OR REPLACE INTO blocked_identities VALUES(?,?)",
            (row["installation_id"], now),
        )
    else:
        connection.execute(
            "DELETE FROM blocked_identities WHERE installation_id=?",
            (row["installation_id"],),
        )
    if state != "active":
        connection.execute(
            "UPDATE commands SET status='expired' WHERE device_id=? AND status NOT IN ('succeeded','failed','expired')",
            (device_id,),
        )
    rollouts.audit(
        connection,
        session["github_login"],
        "set_device_state",
        {"installation_id": row["installation_id"], "state": state},
    )
    connection.commit()
    return {"status": state}


@bp.post("/api/cleanup")
@admin_required
@csrf_required
def cleanup_devices():
    connection = get_db()
    count = telemetry.cleanup(connection, now_epoch())
    rollouts.audit(
        connection,
        session["github_login"],
        "cleanup_registration_only",
        {"deleted": count},
    )
    connection.commit()
    return {"deleted": count}


@bp.post("/api/rollouts/<int:rollout_id>/retry")
@admin_required
@csrf_required
def retry_rollout(rollout_id):
    return rollouts.retry_failed(get_db(), rollout_id, session["github_login"])


@bp.post("/api/rollouts/<int:rollout_id>/finish-candidate")
@admin_required
@csrf_required
def finish_candidate_rollout(rollout_id):
    return rollouts.finish_candidate(get_db(), rollout_id, session["github_login"])
