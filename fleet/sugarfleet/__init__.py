"""Application factory for the SugarClock fleet service."""

import os
import secrets

from flask import Flask
from werkzeug.middleware.proxy_fix import ProxyFix

from . import admin, auth, db, device


def create_app(test_config=None):
    app = Flask(__name__, instance_relative_config=True)
    app.config.from_mapping(
        DATABASE=os.environ.get("FLEET_DATABASE", "/data/sugarfleet.db"),
        SECRET_KEY=os.environ.get("FLEET_SECRET_KEY") or secrets.token_hex(32),
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
        TRUSTED_PROXY_HOPS=int(os.environ.get("FLEET_TRUSTED_PROXY_HOPS", "0")),
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

    trusted_proxy_hops = app.config["TRUSTED_PROXY_HOPS"]
    if trusted_proxy_hops < 0 or trusted_proxy_hops > 3:
        raise ValueError("FLEET_TRUSTED_PROXY_HOPS must be between 0 and 3")
    if trusted_proxy_hops:
        # Trust only the configured number of right-most X-Forwarded-For hops.
        # Never enable this for a service directly reachable without that proxy.
        app.wsgi_app = ProxyFix(app.wsgi_app, x_for=trusted_proxy_hops)

    os.makedirs(os.path.dirname(os.path.abspath(app.config["DATABASE"])), exist_ok=True)
    db.init_app(app)
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
    return app
