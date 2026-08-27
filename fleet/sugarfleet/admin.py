"""Private administrator UI and JSON API."""

import json
import uuid

from flask import Blueprint, current_app, redirect, render_template, request, session, url_for

from .auth import admin_required, csrf_required
from .db import get_db
from .releases import import_manifest, synchronize
from .util import ApiError, connectivity_state, error_response, json_body, json_text, now_epoch
from .validation import validate_command, validate_expiration


bp = Blueprint("admin", __name__, url_prefix="/admin")


@bp.errorhandler(ApiError)
def handle_api_error(error):
    return error_response(error)


def _device_json(row, *, detail=False):
    value = {
        "id": row["id"],
        "installation_id": row["installation_id"],
        "friendly_name": row["friendly_name"],
        "location_label": row["location_label"],
        "verification_state": row["verification_state"],
        "connectivity": connectivity_state(row["last_seen"], row["retired_at"]),
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
                "previous_partition_available": bool(row["previous_partition_available"]),
                "maintenance_window": json.loads(row["maintenance_window_json"] or "null"),
                "config_revision": row["config_revision"],
                "config_hash": row["config_hash"],
                "uptime_seconds": row["uptime_seconds"],
                "free_heap_bucket": row["free_heap_bucket"],
                "wifi_signal_bucket": row["wifi_signal_bucket"],
                "battery_percent": row["battery_percent"],
                "charging": bool(row["charging"]) if row["charging"] is not None else None,
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
    return redirect(url_for("admin.device_list_page"))


@bp.get("/devices")
@admin_required
def device_list_page():
    devices = [_device_json(row) for row in get_db().execute("SELECT * FROM devices ORDER BY last_seen DESC")]
    return render_template("devices.html", devices=devices)


@bp.get("/devices/<int:device_id>")
@admin_required
def device_detail_page(device_id):
    connection = get_db()
    device = connection.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
    if not device:
        return render_template("not_found.html"), 404
    commands = connection.execute(
        "SELECT * FROM commands WHERE device_id=? ORDER BY created_at DESC LIMIT 30", (device_id,)
    ).fetchall()
    return render_template(
        "device_detail.html",
        device=_device_json(device, detail=True),
        commands=[_command_json(row) for row in commands],
    )


@bp.get("/releases")
@admin_required
def releases_page():
    releases = get_db().execute("SELECT * FROM releases ORDER BY published_at DESC, channel").fetchall()
    return render_template("releases.html", releases=releases)


@bp.get("/api/devices")
@admin_required
def api_devices():
    state = request.args.get("state")
    channel = request.args.get("channel")
    devices = [_device_json(row) for row in get_db().execute("SELECT * FROM devices ORDER BY last_seen DESC")]
    if state:
        devices = [device for device in devices if device["connectivity"] == state]
    if channel:
        devices = [device for device in devices if device["channel"] == channel]
    return {"devices": devices}


@bp.get("/api/devices/<int:device_id>")
@admin_required
def api_device(device_id):
    connection = get_db()
    row = connection.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
    if not row:
        raise ApiError("device_not_found", "device not found", 404)
    commands = connection.execute(
        "SELECT * FROM commands WHERE device_id=? ORDER BY created_at DESC LIMIT 100", (device_id,)
    ).fetchall()
    return {"device": _device_json(row, detail=True), "commands": [_command_json(item) for item in commands]}


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
    device = connection.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
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
    updated = connection.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
    return {"status": "updated", "device": _device_json(updated, detail=True)}


@bp.post("/api/devices/<int:device_id>/commands")
@admin_required
@csrf_required
def queue_command(device_id):
    value = json_body()
    command_type = value.get("type")
    payload = value.get("payload", {})
    validate_command(command_type, payload)
    now = now_epoch()
    expires_at = validate_expiration(value.get("expires_at"), now)
    connection = get_db()
    device = connection.execute("SELECT * FROM devices WHERE id=?", (device_id,)).fetchone()
    if not device or device["retired_at"] is not None:
        raise ApiError("device_not_found", "active device not found", 404)
    command_id = str(uuid.uuid4())
    summary = {"command_id": command_id, "type": command_type, "override_window": bool(payload.get("override_window"))}
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
    return {"command": {"id": command_id, "status": "queued", "expires_at": expires_at}}, 201


@bp.get("/api/releases")
@admin_required
def api_releases():
    rows = get_db().execute("SELECT * FROM releases ORDER BY published_at DESC, channel").fetchall()
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
            }
            for row in rows
        ]
    }


@bp.post("/api/releases/import")
@admin_required
@csrf_required
def api_import_release():
    value = json_body()
    if not isinstance(value.get("manifest"), dict) or not isinstance(value.get("manifest_url"), str):
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
    return {"release_id": release_id, "status": "imported" if created else "unchanged"}, 201 if created else 200


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
        raise ApiError("github_unavailable", "GitHub release synchronization failed", 502) from error
    return result
