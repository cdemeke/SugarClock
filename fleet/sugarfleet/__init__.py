"""Application factory for the SugarClock fleet service."""

import os
import secrets

from flask import Flask
from werkzeug.middleware.proxy_fix import ProxyFix

from . import admin, auth, db, device, enrollment, location_worker


def create_app(test_config=None):
    insecure_mode = os.environ.get("FLEET_INSECURE_COOKIES", "") == "1"
    configured_secret = os.environ.get("FLEET_SECRET_KEY")
    supplied_test_secret = bool(test_config and test_config.get("SECRET_KEY"))
    if not configured_secret and not insecure_mode and not supplied_test_secret:
        raise RuntimeError("FLEET_SECRET_KEY is required unless FLEET_INSECURE_COOKIES=1")
    app = Flask(__name__, instance_relative_config=True)
    app.config.from_mapping(
        DATABASE=os.environ.get("FLEET_DATABASE", "/data/sugarfleet.db"),
        SECRET_KEY=configured_secret or secrets.token_hex(32),
        DEVICE_CREDENTIAL_PEPPER=os.environ.get("FLEET_DEVICE_CREDENTIAL_PEPPER", ""),
        GITHUB_CLIENT_ID=os.environ.get("FLEET_GITHUB_CLIENT_ID", ""),
        GITHUB_CLIENT_SECRET=os.environ.get("FLEET_GITHUB_CLIENT_SECRET", ""),
        GITHUB_ALLOWLIST=os.environ.get("FLEET_GITHUB_ALLOWLIST", ""),
        GITHUB_REPOSITORY=os.environ.get("FLEET_GITHUB_REPOSITORY", "cdemeke/SugarClock"),
        GITHUB_API_TOKEN=os.environ.get("FLEET_GITHUB_API_TOKEN", ""),
        PUBLIC_BASE_URL=os.environ.get("FLEET_PUBLIC_BASE_URL", ""),
        DEVICE_CHECKIN_SECONDS=int(os.environ.get("FLEET_DEVICE_CHECKIN_SECONDS", "120")),
        IP_GEOLOCATION_ENABLED=os.environ.get("FLEET_IP_GEOLOCATION_ENABLED", "") == "1",
        IP_GEOLOCATION_REFRESH_SECONDS=int(
            os.environ.get("FLEET_IP_GEOLOCATION_REFRESH_SECONDS", str(7 * 86400))
        ),
        IP_GEOLOCATION_QUEUE_SIZE=int(os.environ.get("FLEET_IP_GEOLOCATION_QUEUE_SIZE", "128")),
        TRUSTED_PROXY_HOPS=int(os.environ.get("FLEET_TRUSTED_PROXY_HOPS", "0")),
        ENROLLMENT_REQUIRED=os.environ.get("FLEET_ENROLLMENT_REQUIRED", "1") != "0",
        ENROLLMENT_MAX_DEVICES=int(os.environ.get("FLEET_ENROLLMENT_MAX_DEVICES", "1000")),
        ENROLLMENT_RATE_LIMIT=int(os.environ.get("FLEET_ENROLLMENT_RATE_LIMIT", "10")),
        ENROLLMENT_RATE_WINDOW_SECONDS=int(os.environ.get("FLEET_ENROLLMENT_RATE_WINDOW_SECONDS", "60")),
        OTA_PUBLIC_KEYS_DIR=os.environ.get(
            "FLEET_OTA_PUBLIC_KEYS_DIR",
            os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "keys")),
        ),
        SESSION_COOKIE_HTTPONLY=True,
        SESSION_COOKIE_SAMESITE="Lax",
        SESSION_COOKIE_SECURE=os.environ.get("FLEET_INSECURE_COOKIES", "") != "1",
        MAX_CONTENT_LENGTH=32 * 1024,
    )
    if test_config:
        app.config.update(test_config)
    if not app.config["DEVICE_CREDENTIAL_PEPPER"]:
        app.config["DEVICE_CREDENTIAL_PEPPER"] = app.config["SECRET_KEY"]
    if len(app.config["SECRET_KEY"]) < 32:
        raise ValueError("FLEET_SECRET_KEY must contain at least 32 characters")
    if len(app.config["DEVICE_CREDENTIAL_PEPPER"]) < 32:
        raise ValueError("FLEET_DEVICE_CREDENTIAL_PEPPER must contain at least 32 characters")
    for name in ("ENROLLMENT_MAX_DEVICES", "ENROLLMENT_RATE_LIMIT", "ENROLLMENT_RATE_WINDOW_SECONDS"):
        if app.config[name] <= 0:
            raise ValueError(f"{name} must be positive")
    if app.config["IP_GEOLOCATION_QUEUE_SIZE"] <= 0:
        raise ValueError("FLEET_IP_GEOLOCATION_QUEUE_SIZE must be positive")

    trusted_proxy_hops = app.config["TRUSTED_PROXY_HOPS"]
    if trusted_proxy_hops < 0 or trusted_proxy_hops > 3:
        raise ValueError("FLEET_TRUSTED_PROXY_HOPS must be between 0 and 3")
    if trusted_proxy_hops:
        # Trust only the configured number of right-most X-Forwarded-For hops.
        # Never enable this for a service directly reachable without that proxy.
        app.wsgi_app = ProxyFix(
            app.wsgi_app,
            x_for=trusted_proxy_hops,
            x_proto=trusted_proxy_hops,
            x_host=trusted_proxy_hops,
        )
    if app.config["IP_GEOLOCATION_ENABLED"] and not trusted_proxy_hops and not app.config.get("TESTING"):
        raise ValueError("IP geolocation requires FLEET_TRUSTED_PROXY_HOPS behind the production proxy")

    os.makedirs(os.path.dirname(os.path.abspath(app.config["DATABASE"])), exist_ok=True)
    db.init_app(app)
    enrollment.init_app(app)
    auth.init_app(app)
    app.register_blueprint(device.bp)
    app.register_blueprint(admin.bp)

    @app.get("/healthz")
    def healthz():
        return {"status": "ok"}

    @app.errorhandler(413)
    def request_too_large(_error):
        return {"error": {"code": "request_too_large", "message": "request body is too large"}}, 413

    with app.app_context():
        db.migrate()
    location_worker.init_app(app)
    return app
