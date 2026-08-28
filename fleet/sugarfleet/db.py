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
    try:
        # Acquire the write lock before reading migration state. Concurrent
        # workers then re-read after the first process commits instead of both
        # attempting the same schema change.
        connection.execute("BEGIN IMMEDIATE")
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
            statement = ""
            for line in script.splitlines(keepends=True):
                statement += line
                if sqlite3.complete_statement(statement):
                    if statement.strip():
                        connection.execute(statement)
                    statement = ""
            if statement.strip():
                raise sqlite3.OperationalError(f"incomplete migration statement in {path.name}")
            connection.execute(
                "INSERT INTO schema_migrations(version, applied_at) VALUES (?, unixepoch())",
                (path.name,),
            )
        connection.commit()
    except Exception:
        connection.rollback()
        raise


def init_app(app):
    app.teardown_appcontext(close_db)
