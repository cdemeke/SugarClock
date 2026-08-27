"""Bounded, administrator-controlled device enrollment."""

import threading
from collections import defaultdict, deque

from flask import current_app

from .db import get_db
from .util import now_epoch


class RegistrationLimiter:
    def __init__(self):
        self._events = defaultdict(deque)
        self._lock = threading.Lock()

    def allow(self, source, now, limit, window_seconds):
        source = (source or "unknown")[:128]
        with self._lock:
            events = self._events[source]
            cutoff = now - window_seconds
            while events and events[0] <= cutoff:
                events.popleft()
            if len(events) >= limit:
                return False
            events.append(now)
            if len(self._events) > 4096:
                for key in list(self._events):
                    if not self._events[key] or self._events[key][-1] <= cutoff:
                        self._events.pop(key, None)
            return True


def init_app(app):
    app.extensions["registration_limiter"] = RegistrationLimiter()


def enrollment_until(connection=None):
    connection = connection or get_db()
    row = connection.execute(
        "SELECT value FROM fleet_settings WHERE key='enrollment_until'"
    ).fetchone()
    return int(row[0]) if row else 0


def enrollment_is_open(connection=None, now=None):
    if not current_app.config["ENROLLMENT_REQUIRED"]:
        return True
    return enrollment_until(connection) > (now if now is not None else now_epoch())


def set_enrollment_until(value, connection=None):
    connection = connection or get_db()
    connection.execute(
        "INSERT INTO fleet_settings(key, value) VALUES ('enrollment_until', ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        (str(int(value)),),
    )


def registration_rate_allowed(source, now=None):
    return current_app.extensions["registration_limiter"].allow(
        source,
        now if now is not None else now_epoch(),
        current_app.config["ENROLLMENT_RATE_LIMIT"],
        current_app.config["ENROLLMENT_RATE_WINDOW_SECONDS"],
    )
