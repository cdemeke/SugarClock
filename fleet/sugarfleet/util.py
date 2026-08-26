import json
import re
import time
import uuid

from flask import jsonify, request


SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class ApiError(ValueError):
    def __init__(self, code, message, status=400):
        super().__init__(message)
        self.code = code
        self.message = message
        self.status = status


def error_response(error):
    return jsonify(error={"code": error.code, "message": error.message}), error.status


def json_body():
    if not request.is_json:
        raise ApiError("json_required", "content type must be application/json", 415)
    value = request.get_json(silent=True)
    if not isinstance(value, dict):
        raise ApiError("invalid_json", "request body must be a JSON object")
    return value


def require_fields(value, fields):
    missing = sorted(set(fields) - set(value))
    if missing:
        raise ApiError("missing_field", "missing required field: " + missing[0])


def valid_uuid(value):
    try:
        return str(uuid.UUID(value)) == value.lower()
    except (AttributeError, ValueError):
        return False


def json_text(value):
    return json.dumps(value, separators=(",", ":"), sort_keys=True)


def now_epoch():
    return int(time.time())


def connectivity_state(last_seen, retired_at=None, now=None):
    if retired_at is not None:
        return "retired"
    age = (now if now is not None else now_epoch()) - last_seen
    if age < 5 * 60:
        return "online"
    if age < 30 * 60:
        return "delayed"
    if age < 30 * 24 * 60 * 60:
        return "offline"
    return "dormant"
