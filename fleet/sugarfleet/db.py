"""SQLite connection and ordered migration support."""

import sqlite3
from pathlib import Path

from flask import current_app, g


def get_db():
    if "db" not in g:
        connection = sqlite3.connect(current_app.config["DATABASE"])
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys = ON")
        connection.execute("PRAGMA journal_mode = WAL")
        connection.execute("PRAGMA busy_timeout = 5000")
        g.db = connection
    return g.db


def close_db(_error=None):
    connection = g.pop("db", None)
    if connection is not None:
        connection.close()


def migrate():
    connection = get_db()
    connection.execute(
        "CREATE TABLE IF NOT EXISTS schema_migrations "
        "(version TEXT PRIMARY KEY, applied_at INTEGER NOT NULL)"
    )
    applied = {row[0] for row in connection.execute("SELECT version FROM schema_migrations")}
    migrations = Path(__file__).with_name("migrations")
    for path in sorted(migrations.glob("*.sql")):
        if path.name in applied:
            continue
        script = path.read_text(encoding="utf-8")
        connection.executescript(
            "BEGIN IMMEDIATE;\n"
            + script
            + "\nINSERT INTO schema_migrations(version, applied_at) "
              f"VALUES ('{path.name}', unixepoch());\nCOMMIT;"
        )


def init_app(app):
    app.teardown_appcontext(close_db)
