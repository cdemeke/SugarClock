"""Shared bounded new-installation registration limits."""

from flask import current_app
from .db import get_db
from .util import now_epoch


def registration_rate_allowed(source, now=None):
    """Shared SQLite limiter with fixed windows, HMAC source keys and bounded rows."""
    import hashlib
    import hmac

    now = now if now is not None else now_epoch()
    connection = get_db()
    window = current_app.config["ENROLLMENT_RATE_WINDOW_SECONDS"]
    source = hmac.new(
        current_app.config["DEVICE_CREDENTIAL_PEPPER"].encode(),
        (source or "unknown").encode(),
        hashlib.sha256,
    ).hexdigest()
    connection.execute("BEGIN IMMEDIATE")
    connection.execute(
        "DELETE FROM registration_limits WHERE window_start<=?", (now - window,)
    )
    for key, limit in (
        ("global", current_app.config["ENROLLMENT_GLOBAL_RATE_LIMIT"]),
        (source, current_app.config["ENROLLMENT_RATE_LIMIT"]),
    ):
        row = connection.execute(
            "SELECT attempts FROM registration_limits WHERE source=?", (key,)
        ).fetchone()
        if row and row[0] >= limit:
            connection.commit()
            return False
    count = connection.execute("SELECT COUNT(*) FROM registration_limits").fetchone()[0]
    if (
        count >= 4096
        and not connection.execute(
            "SELECT 1 FROM registration_limits WHERE source=?", (source,)
        ).fetchone()
    ):
        connection.commit()
        return False
    for key in ("global", source):
        connection.execute(
            "INSERT INTO registration_limits VALUES(?,?,1) ON CONFLICT(source) DO UPDATE SET attempts=attempts+1",
            (key, now),
        )
    connection.commit()
    return True
