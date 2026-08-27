"""Authenticated polling API used by clocks."""

import json

from flask import Blueprint, current_app, request

from .db import get_db
from .geolocation import lookup_approximate_location, public_ip
from .security import hash_device_credential, valid_device_credential, verify_device_credential
from .util import ApiError, error_response, json_body, json_text, now_epoch
from .validation import validate_checkin, validate_register, validate_result


bp = Blueprint("device", __name__, url_prefix="/device/v1")


def _refresh_detected_location(connection, device, now):
    """Best-effort coarse geolocation without retaining the source IP."""
    if not current_app.config["IP_GEOLOCATION_ENABLED"]:
        return
    address = public_ip(request.remote_addr)
    if not address:
        return
    checked_at = device["detected_location_checked_at"] or 0
    refresh_seconds = current_app.config["IP_GEOLOCATION_REFRESH_SECONDS"]
    if checked_at and now - checked_at < refresh_seconds:
        return
    location = lookup_approximate_location(address)
    if location:
        connection.execute(
            "UPDATE devices SET detected_city=?, detected_region=?, detected_country_code=?, "
            "detected_location_checked_at=? WHERE id=?",
            (
                location["city"],
                location["region"],
                location["country_code"],
                now,
                device["id"],
            ),
        )
    else:
        # Record a public-IP lookup attempt so a provider outage cannot cause a
        # request on every device check-in. Existing detected data is retained.
        connection.execute(
            "UPDATE devices SET detected_location_checked_at=? WHERE id=?",
            (now, device["id"]),
        )


def _credential():
    authorization = request.headers.get("Authorization", "")
    if not authorization.startswith("Bearer "):
        raise ApiError("device_auth_required", "Bearer device credential is required", 401)
    credential = authorization[7:]
    if not valid_device_credential(credential):
        raise ApiError("invalid_device_credential", "device credential is invalid", 401)
    return credential


def _authenticated_device(installation_id):
    credential = _credential()
    device = get_db().execute(
        "SELECT * FROM devices WHERE installation_id = ?", (installation_id,)
    ).fetchone()
    if device is None or not verify_device_credential(credential, device["credential_hash"]):
        raise ApiError("invalid_device_credential", "device credential is invalid", 401)
    if device["retired_at"] is not None:
        raise ApiError("device_retired", "device enrollment is retired", 403)
    return device


@bp.errorhandler(ApiError)
def handle_api_error(error):
    return error_response(error)


@bp.post("/register")
def register():
    value = json_body()
    validate_register(value)
    credential = _credential()
    connection = get_db()
    existing = connection.execute(
        "SELECT * FROM devices WHERE installation_id = ?", (value["installation_id"],)
    ).fetchone()
    now = now_epoch()
    if existing:
        if not verify_device_credential(credential, existing["credential_hash"]):
            raise ApiError("installation_already_registered", "installation ID is already registered", 409)
        if existing["retired_at"] is not None:
            raise ApiError("device_retired", "device enrollment is retired", 403)
        connection.execute(
            "UPDATE devices SET last_seen=?, hardware=?, firmware_version=?, timezone=?, management_protocol=? "
            "WHERE id=?",
            (now, value["hardware"], value["firmware_version"], value["timezone"], value["management_protocol"], existing["id"]),
        )
        connection.commit()
        status = "already_registered"
        response_status = 200
    else:
        connection.execute(
            "INSERT INTO devices "
            "(installation_id, credential_hash, hardware, management_protocol, first_seen, last_seen, "
            "firmware_version, channel, timezone) VALUES (?, ?, ?, ?, ?, ?, ?, 'stable', ?)",
            (
                value["installation_id"],
                hash_device_credential(credential),
                value["hardware"],
                value["management_protocol"],
                now,
                now,
                value["firmware_version"],
                value["timezone"],
            ),
        )
        connection.commit()
        status = "registered"
        response_status = 201
    device = connection.execute(
        "SELECT * FROM devices WHERE installation_id = ?", (value["installation_id"],)
    ).fetchone()
    _refresh_detected_location(connection, device, now)
    connection.commit()
    return {
        "status": status,
        "verification_state": "unverified",
        "next_checkin_seconds": current_app.config["DEVICE_CHECKIN_SECONDS"],
        "server_time": now,
    }, response_status


@bp.post("/check-in")
def check_in():
    value = json_body()
    validate_checkin(value)
    device = _authenticated_device(value["installation_id"])
    now = now_epoch()
    connection = get_db()
    _refresh_detected_location(connection, device, now)
    connection.execute(
        "UPDATE devices SET last_seen=?, firmware_version=?, running_partition=?, boot_partition=?, "
        "previous_partition=?, previous_partition_available=?, channel=?, timezone=COALESCE(?, timezone), "
        "reported_nickname=?, reported_location=?, "
        "maintenance_window_json=?, config_revision=?, config_hash=?, last_ota_result=?, "
        "last_rollback_result=?, uptime_seconds=?, free_heap_bucket=?, wifi_signal_bucket=?, "
        "battery_percent=?, charging=?, health_json=? WHERE id=?",
        (
            now,
            value["firmware_version"],
            value.get("running_partition"),
            value.get("boot_partition"),
            value.get("previous_partition"),
            int(bool(value.get("previous_partition_available", False))),
            value["channel"],
            value.get("timezone"),
            value.get("device_nickname", device["reported_nickname"]).strip(),
            value.get("device_location", device["reported_location"]).strip(),
            json_text(value.get("maintenance_window")) if value.get("maintenance_window") else None,
            value.get("config_revision"),
            value.get("config_hash"),
            value.get("last_ota_result"),
            value.get("last_rollback_result"),
            value["uptime_seconds"],
            value.get("free_heap_bucket"),
            value.get("wifi_signal_bucket"),
            value.get("battery_percent"),
            int(value["charging"]) if "charging" in value else None,
            json_text(value.get("health_codes", [])),
            device["id"],
        ),
    )
    connection.execute(
        "UPDATE commands SET status='expired' WHERE device_id=? AND status IN ('queued','delivered') "
        "AND expires_at<=?",
        (device["id"], now),
    )
    pending = connection.execute(
        "SELECT id, type, payload_json, created_at, expires_at FROM commands "
        "WHERE device_id=? AND status IN ('queued','delivered') AND expires_at>? "
        "ORDER BY created_at, id LIMIT 16",
        (device["id"], now),
    ).fetchall()
    if pending:
        command_ids = [row["id"] for row in pending]
        placeholders = ",".join("?" for _ in command_ids)
        connection.execute(
            f"UPDATE commands SET status='delivered', delivered_at=COALESCE(delivered_at, ?), "
            f"attempt_count=attempt_count+1 WHERE id IN ({placeholders})",
            (now, *command_ids),
        )
    connection.commit()
    return {
        "server_time": now,
        "next_checkin_seconds": current_app.config["DEVICE_CHECKIN_SECONDS"],
        "commands": [
            {
                "id": row["id"],
                "type": row["type"],
                "payload": json.loads(row["payload_json"]),
                "created_at": row["created_at"],
                "expires_at": row["expires_at"],
            }
            for row in pending
        ],
    }


@bp.post("/commands/<command_id>/result")
def command_result(command_id):
    value = json_body()
    validate_result(value)
    device = _authenticated_device(value["installation_id"])
    connection = get_db()
    command = connection.execute(
        "SELECT * FROM commands WHERE id=? AND device_id=?", (command_id, device["id"])
    ).fetchone()
    if command is None:
        raise ApiError("command_not_found", "command does not belong to this device", 404)
    result = {
        "reason": value.get("reason"),
        "config_revision": value.get("config_revision"),
        "firmware_version": value.get("firmware_version"),
    }
    final = {"succeeded", "failed"}
    if command["status"] in final:
        stored = json.loads(command["result_json"] or "{}")
        if command["status"] == value["status"] and stored == result:
            return {"status": "already_recorded"}
        raise ApiError("command_already_final", "command already has a final result", 409)
    connection.execute(
        "UPDATE commands SET status=?, acknowledged_at=?, result_json=? WHERE id=?",
        (value["status"], now_epoch(), json_text(result), command_id),
    )
    connection.execute(
        "UPDATE audit_events SET result=? WHERE id=?",
        (value["status"], command["audit_event_id"]),
    )
    connection.commit()
    return {"status": "recorded"}
