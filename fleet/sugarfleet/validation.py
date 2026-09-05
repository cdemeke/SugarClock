"""Protocol validation shared by device and administrator endpoints."""

import datetime as dt
import re

from .util import ApiError, SEMVER_RE, SHA256_RE, require_fields, valid_uuid


CHANNELS = {"stable", "preview"}
COMMAND_TYPES = {
    "config_patch",
    "set_channel",
    "set_maintenance_window",
    "ota_check",
    "ota_install",
    "ota_pause",
    "ota_rollback_previous",
    "restart",
    "notify",
}
RESULT_STATUSES = {"accepted", "deferred", "succeeded", "failed"}
SECRET_CONFIG_FIELDS = {
    "wifi_password",
    "wifi_eap_password",
    "auth_token",
    "dexcom_username",
    "dexcom_password",
    "weather_api_key",
}
PHASE4_PROTECTED_FIELDS = SECRET_CONFIG_FIELDS | {
    "wifi_ssid",
    "wifi_security",
    "wifi_eap_method",
    "wifi_identity",
    "wifi_anon_identity",
    "wifi_validate_ca",
    "server_url",
}

# Phase 1 accepts typed, non-secret patches in the queue. Secret replacement and
# transactional Wi-Fi application intentionally remain disabled until Phase 4.
CONFIG_FIELDS = {
    "wifi_ssid": (str, 1, 63),
    "wifi_security": (int, 0, 1),
    "wifi_eap_method": (int, 0, 1),
    "wifi_identity": (str, 0, 127),
    "wifi_anon_identity": (str, 0, 127),
    "wifi_validate_ca": (bool, None, None),
    "data_source": (int, 0, 2),
    "server_url": (str, 0, 255),
    "dexcom_us": (bool, None, None),
    "poll_interval_sec": (int, 15, 3600),
    "brightness": (int, 0, 255),
    "auto_brightness": (bool, None, None),
    "show_delta": (bool, None, None),
    "use_mmol": (bool, None, None),
    "thresh_urgent_low": (int, 20, 400),
    "thresh_low": (int, 20, 400),
    "thresh_high": (int, 20, 400),
    "thresh_urgent_high": (int, 20, 400),
    "timezone": (str, 1, 63),
    "use_24h": (bool, None, None),
    "time_display_enabled": (bool, None, None),
    "default_mode": (int, 0, 3),
    "ambient_style": (int, 0, 2),
    "ambient_creature": (int, 0, 1),
    "ambient_character": (int, 0, 3),
    "ambient_enabled": (bool, None, None),
    "ambient_seasonal": (bool, None, None),
    "alert_enabled": (bool, None, None),
    "alert_low": (int, 20, 400),
    "alert_high": (int, 20, 400),
    "alert_snooze_min": (int, 1, 1440),
    "color_urgent_low": (int, 0, 0xFFFFFF),
    "color_low": (int, 0, 0xFFFFFF),
    "color_in_range": (int, 0, 0xFFFFFF),
    "color_high": (int, 0, 0xFFFFFF),
    "color_urgent_high": (int, 0, 0xFFFFFF),
    "color_stale": (int, 0, 0xFFFFFF),
    "color_clock": (int, 0, 0xFFFFFF),
    "color_weather": (int, 0, 0xFFFFFF),
    "night_mode_enabled": (bool, None, None),
    "night_start_hour": (int, 0, 23),
    "night_end_hour": (int, 0, 23),
    "night_brightness": (int, 0, 255),
    "stale_timeout_min": (int, 1, 1440),
    "weather_enabled": (bool, None, None),
    "weather_city": (str, 1, 63),
    "weather_use_f": (bool, None, None),
    "weather_poll_min": (int, 5, 1440),
    "date_on_time_screen": (bool, None, None),
    "date_format": (int, 0, 2),
    "timer_enabled": (bool, None, None),
    "timer_work_min": (int, 1, 1440),
    "timer_break_min": (int, 1, 1440),
    "timer_long_break_min": (int, 1, 1440),
    "timer_sessions": (int, 1, 20),
    "timer_buzzer": (bool, None, None),
    "stopwatch_enabled": (bool, None, None),
    "notify_enabled": (bool, None, None),
    "notify_default_duration": (int, 5, 300),
    "notify_allow_buzzer": (bool, None, None),
    "sysmon_enabled": (bool, None, None),
    "sysmon_label": (str, 1, 7),
    "sysmon_display_mode": (int, 0, 1),
    "sysmon_warn_pct": (int, 0, 100),
    "sysmon_crit_pct": (int, 0, 100),
    "auto_cycle_enabled": (bool, None, None),
    "auto_cycle_sec": (int, 3, 300),
    "countdown_enabled": (bool, None, None),
    "countdown_name": (str, 0, 15),
    "countdown_target": (int, 0, 0xFFFFFFFF),
}


def _is_type(value, expected):
    if expected is int:
        return isinstance(value, int) and not isinstance(value, bool)
    return isinstance(value, expected)


def validate_register(value):
    require_fields(value, {"installation_id", "hardware", "firmware_version", "timezone", "management_protocol"})
    if not valid_uuid(value["installation_id"]):
        raise ApiError("invalid_installation_id", "installation_id must be a canonical UUID")
    if not isinstance(value["hardware"], str) or not 1 <= len(value["hardware"]) <= 64:
        raise ApiError("invalid_hardware", "hardware must contain 1-64 characters")
    if not SEMVER_RE.fullmatch(value["firmware_version"] if isinstance(value["firmware_version"], str) else ""):
        raise ApiError("invalid_firmware_version", "firmware_version must be semantic version x.y.z")
    if not isinstance(value["timezone"], str) or not 1 <= len(value["timezone"]) <= 64:
        raise ApiError("invalid_timezone", "timezone must contain 1-64 characters")
    if value["management_protocol"] != 1:
        raise ApiError("unsupported_protocol", "management_protocol 1 is required", 422)


def validate_maintenance_window(value):
    require_fields(value, {"timezone", "days", "start", "end", "automatic_install"})
    if not isinstance(value["timezone"], str) or not 1 <= len(value["timezone"]) <= 64:
        raise ApiError("invalid_timezone", "timezone must contain 1-64 characters")
    days = value["days"]
    if not isinstance(days, list) or not days or any(type(day) is not int or day < 0 or day > 6 for day in days):
        raise ApiError("invalid_days", "days must be a non-empty list of integers 0-6")
    if len(days) != len(set(days)):
        raise ApiError("invalid_days", "days cannot contain duplicates")
    for field in ("start", "end"):
        if not isinstance(value[field], str) or not re.fullmatch(r"(?:[01]\d|2[0-3]):[0-5]\d", value[field]):
            raise ApiError("invalid_time", f"{field} must use 24-hour HH:MM")
    if type(value["automatic_install"]) is not bool:
        raise ApiError("invalid_automatic_install", "automatic_install must be boolean")


def validate_checkin(value):
    require_fields(value, {"installation_id", "firmware_version", "channel", "uptime_seconds"})
    if not valid_uuid(value["installation_id"]):
        raise ApiError("invalid_installation_id", "installation_id must be a canonical UUID")
    if not SEMVER_RE.fullmatch(value["firmware_version"] if isinstance(value["firmware_version"], str) else ""):
        raise ApiError("invalid_firmware_version", "firmware_version must be semantic version x.y.z")
    if value["channel"] not in CHANNELS:
        raise ApiError("invalid_channel", "channel must be stable or preview")
    if type(value["uptime_seconds"]) is not int or value["uptime_seconds"] < 0:
        raise ApiError("invalid_uptime", "uptime_seconds must be a non-negative integer")
    if "maintenance_window" in value:
        validate_maintenance_window(value["maintenance_window"])
    if "battery_percent" in value and (
        type(value["battery_percent"]) is not int or not 0 <= value["battery_percent"] <= 100
    ):
        raise ApiError("invalid_battery", "battery_percent must be 0-100")
    if "health_codes" in value:
        codes = value["health_codes"]
        if not isinstance(codes, list) or len(codes) > 16 or any(
            not isinstance(code, str) or not re.fullmatch(r"[a-z0-9_]{1,48}", code) for code in codes
        ):
            raise ApiError("invalid_health_codes", "health_codes must contain sanitized identifiers")


def validate_config_patch(changes):
    if not isinstance(changes, dict) or not changes:
        raise ApiError("invalid_config_patch", "changes must be a non-empty object")
    unknown = sorted(set(changes) - set(CONFIG_FIELDS) - PHASE4_PROTECTED_FIELDS)
    if unknown:
        raise ApiError("unknown_config_field", "unknown configuration field: " + unknown[0])
    protected = sorted(set(changes) & PHASE4_PROTECTED_FIELDS)
    if protected:
        raise ApiError(
            "protected_patch_not_enabled",
            "secret and connectivity patches are disabled until encrypted transactional support is implemented",
            422,
        )
    for name, value in changes.items():
        expected, minimum, maximum = CONFIG_FIELDS[name]
        if not _is_type(value, expected):
            raise ApiError("invalid_config_value", f"{name} has the wrong type")
        if expected is str and not minimum <= len(value) <= maximum:
            raise ApiError("invalid_config_value", f"{name} length is out of range")
        if expected is int and not minimum <= value <= maximum:
            raise ApiError("invalid_config_value", f"{name} is out of range")


def validate_command(command_type, payload):
    if command_type not in COMMAND_TYPES:
        raise ApiError("invalid_command_type", "unsupported command type")
    if not isinstance(payload, dict):
        raise ApiError("invalid_command_payload", "payload must be an object")
    if command_type == "config_patch":
        require_fields(payload, {"changes"})
        validate_config_patch(payload["changes"])
    elif command_type == "set_channel":
        if payload.get("channel") not in CHANNELS:
            raise ApiError("invalid_channel", "channel must be stable or preview")
    elif command_type == "set_maintenance_window":
        validate_maintenance_window(payload)
    elif command_type in {"ota_check", "ota_rollback_previous"}:
        if set(payload) - {"reason"}:
            raise ApiError("invalid_command_payload", "unexpected command payload field")
    elif command_type == "ota_install":
        require_fields(payload, {"manifest_url", "version", "channel", "sha256", "override_window"})
        if not immutable_https_url(payload["manifest_url"]):
            raise ApiError("invalid_manifest_url", "manifest_url must be an immutable HTTPS release URL")
        if not SEMVER_RE.fullmatch(payload["version"] if isinstance(payload["version"], str) else ""):
            raise ApiError("invalid_version", "version must be semantic version x.y.z")
        if payload["channel"] not in CHANNELS or not SHA256_RE.fullmatch(payload["sha256"] or ""):
            raise ApiError("invalid_release", "channel or firmware hash is invalid")
        _validate_override(payload)
    elif command_type == "ota_pause":
        if type(payload.get("paused")) is not bool:
            raise ApiError("invalid_pause", "paused must be boolean")
    elif command_type == "restart":
        require_fields(payload, {"override_window"})
        _validate_override(payload)
    elif command_type == "notify":
        require_fields(payload, {"message", "duration_seconds"})
        message = payload["message"]
        if not isinstance(message, str) or not 1 <= len(message) <= 120 or any(
            ord(character) < 0x20 or ord(character) > 0x7E for character in message
        ):
            raise ApiError("invalid_notification", "message must be 1-120 printable ASCII characters")
        if "http://" in message.lower() or "https://" in message.lower() or "www." in message.lower():
            raise ApiError("invalid_notification", "notification URLs are not allowed")
        if type(payload["duration_seconds"]) is not int or not 5 <= payload["duration_seconds"] <= 120:
            raise ApiError("invalid_notification", "duration_seconds must be 5-120")
        if "allow_buzzer" in payload and type(payload["allow_buzzer"]) is not bool:
            raise ApiError("invalid_notification", "allow_buzzer must be boolean")


def _validate_override(payload):
    if type(payload.get("override_window")) is not bool:
        raise ApiError("invalid_override", "override_window must be boolean")
    if payload["override_window"] and (
        not isinstance(payload.get("audit_reason"), str) or not 8 <= len(payload["audit_reason"]) <= 240
    ):
        raise ApiError("audit_reason_required", "an 8-240 character audit reason is required for override")


def immutable_https_url(value):
    return (
        isinstance(value, str)
        and value.startswith("https://")
        and len(value) <= 512
        and not any(character in value for character in " @#\r\n")
        and "/latest/" not in value
        and "/releases/latest" not in value
    )


def validate_result(value):
    require_fields(value, {"installation_id", "status"})
    if not valid_uuid(value["installation_id"]):
        raise ApiError("invalid_installation_id", "installation_id must be a canonical UUID")
    if value["status"] not in RESULT_STATUSES:
        raise ApiError("invalid_result_status", "invalid command result status")
    reason = value.get("reason")
    if reason is not None and (
        not isinstance(reason, str) or not re.fullmatch(r"[a-z0-9_]{1,64}", reason)
    ):
        raise ApiError("invalid_result_reason", "reason must be a sanitized identifier")


def validate_expiration(value, now):
    if value is None:
        return now + 24 * 60 * 60
    if type(value) is not int or value <= now or value > now + 7 * 24 * 60 * 60:
        raise ApiError("invalid_expiration", "expires_at must be within the next seven days")
    return value


def validate_published_at(value):
    try:
        dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except (TypeError, ValueError) as error:
        raise ApiError("invalid_published_at", "published_at must be UTC RFC3339") from error
