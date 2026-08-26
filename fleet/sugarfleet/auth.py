"""GitHub OAuth authentication for the single-administrator console."""

import hmac
import json
import secrets
import urllib.parse
import urllib.request
from functools import wraps

from flask import Blueprint, current_app, redirect, request, session, url_for

from .util import ApiError, error_response


bp = Blueprint("auth", __name__, url_prefix="/auth")


def allowlist():
    configured = current_app.config.get("GITHUB_ALLOWLIST", "")
    values = configured if isinstance(configured, (list, tuple, set)) else configured.split(",")
    return {value.strip().lower() for value in values if value.strip()}


def _github_json(url, *, data=None, token=None):
    headers = {"Accept": "application/json", "User-Agent": "SugarClock-Fleet/1"}
    if token:
        headers["Authorization"] = "Bearer " + token
    request_value = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(request_value, timeout=15) as response:
        if response.length is not None and response.length > 64 * 1024:
            raise ApiError("oauth_failed", "GitHub returned an oversized response", 502)
        return json.loads(response.read(64 * 1024))


def admin_required(view):
    @wraps(view)
    def wrapped(*args, **kwargs):
        login = session.get("github_login", "").lower()
        if not login or login not in allowlist():
            if request.path.startswith("/admin/api/"):
                return error_response(ApiError("admin_auth_required", "administrator login required", 401))
            return redirect(url_for("auth.login"))
        return view(*args, **kwargs)

    return wrapped


def csrf_required(view):
    @wraps(view)
    def wrapped(*args, **kwargs):
        expected = session.get("csrf_token", "")
        supplied = request.headers.get("X-CSRF-Token")
        if supplied is None and request.form:
            supplied = request.form.get("csrf_token")
        if not expected or not supplied or not hmac.compare_digest(expected, supplied):
            return error_response(ApiError("csrf_failed", "valid CSRF token required", 403))
        return view(*args, **kwargs)

    return wrapped


@bp.get("/login")
def login():
    if not current_app.config["GITHUB_CLIENT_ID"] or not current_app.config["GITHUB_CLIENT_SECRET"]:
        return error_response(ApiError("oauth_not_configured", "GitHub OAuth is not configured", 503))
    if not allowlist():
        return error_response(ApiError("allowlist_not_configured", "administrator allowlist is empty", 503))
    state = secrets.token_urlsafe(32)
    session["oauth_state"] = state
    callback = _callback_url()
    query = urllib.parse.urlencode(
        {"client_id": current_app.config["GITHUB_CLIENT_ID"], "redirect_uri": callback, "state": state, "scope": "read:user"}
    )
    return redirect("https://github.com/login/oauth/authorize?" + query)


@bp.get("/callback")
def callback():
    expected = session.pop("oauth_state", "")
    supplied = request.args.get("state", "")
    code = request.args.get("code", "")
    if not expected or not supplied or not hmac.compare_digest(expected, supplied) or not code:
        return error_response(ApiError("oauth_state_failed", "OAuth state validation failed", 400))
    body = urllib.parse.urlencode(
        {
            "client_id": current_app.config["GITHUB_CLIENT_ID"],
            "client_secret": current_app.config["GITHUB_CLIENT_SECRET"],
            "code": code,
            "redirect_uri": _callback_url(),
        }
    ).encode("ascii")
    try:
        token_response = _github_json("https://github.com/login/oauth/access_token", data=body)
        token = token_response.get("access_token")
        identity = _github_json("https://api.github.com/user", token=token)
    except (OSError, ValueError, ApiError):
        return error_response(ApiError("oauth_failed", "GitHub authentication failed", 502))
    login_name = identity.get("login", "") if isinstance(identity, dict) else ""
    if login_name.lower() not in allowlist():
        session.clear()
        return error_response(ApiError("admin_not_allowed", "GitHub account is not allowlisted", 403))
    session.clear()
    session["github_login"] = login_name
    session["csrf_token"] = secrets.token_urlsafe(32)
    return redirect(url_for("admin.device_list_page"))


@bp.post("/logout")
@csrf_required
def logout():
    session.clear()
    return redirect(url_for("auth.login"))


def _callback_url():
    base = current_app.config.get("PUBLIC_BASE_URL", "").rstrip("/")
    return base + "/auth/callback" if base else url_for("auth.callback", _external=True)


def init_app(app):
    app.register_blueprint(bp)

    @app.context_processor
    def auth_template_context():
        return {"admin_login": session.get("github_login"), "csrf_token": session.get("csrf_token", "")}
