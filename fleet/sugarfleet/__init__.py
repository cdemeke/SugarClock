"""Application factory for the SugarClock fleet service."""

import os
import secrets

from flask import Flask

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
