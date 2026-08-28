CREATE TABLE devices (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    installation_id TEXT NOT NULL UNIQUE,
    credential_hash TEXT NOT NULL,
    friendly_name TEXT NOT NULL DEFAULT '',
    verification_state TEXT NOT NULL DEFAULT 'unverified'
        CHECK (verification_state IN ('unverified', 'verified')),
    hardware TEXT NOT NULL,
    management_protocol INTEGER NOT NULL,
    first_seen INTEGER NOT NULL,
    last_seen INTEGER NOT NULL,
    retired_at INTEGER,
    firmware_version TEXT NOT NULL,
    running_partition TEXT,
    boot_partition TEXT,
    previous_partition TEXT,
    previous_partition_available INTEGER NOT NULL DEFAULT 0,
    channel TEXT NOT NULL DEFAULT 'stable' CHECK (channel IN ('stable', 'preview')),
    timezone TEXT NOT NULL,
    maintenance_window_json TEXT,
    config_revision TEXT,
    config_hash TEXT,
    last_ota_result TEXT,
    last_rollback_result TEXT,
    uptime_seconds INTEGER,
    free_heap_bucket TEXT,
    wifi_signal_bucket TEXT,
    battery_percent INTEGER,
    charging INTEGER,
    health_json TEXT NOT NULL DEFAULT '[]'
);

CREATE TABLE audit_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    administrator TEXT NOT NULL,
    action TEXT NOT NULL,
    target_device_id INTEGER REFERENCES devices(id),
    summary_json TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    result TEXT NOT NULL
);

CREATE TABLE commands (
    id TEXT PRIMARY KEY,
    device_id INTEGER NOT NULL REFERENCES devices(id),
    type TEXT NOT NULL,
    payload_json TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'queued'
        CHECK (status IN ('queued', 'delivered', 'accepted', 'deferred', 'succeeded', 'failed', 'expired')),
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    delivered_at INTEGER,
    acknowledged_at INTEGER,
    attempt_count INTEGER NOT NULL DEFAULT 0,
    result_json TEXT,
    administrator TEXT NOT NULL,
    audit_event_id INTEGER NOT NULL REFERENCES audit_events(id)
);
CREATE INDEX commands_pending_device ON commands(device_id, status, expires_at);

CREATE TABLE releases (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    version TEXT NOT NULL,
    channel TEXT NOT NULL CHECK (channel IN ('stable', 'preview')),
    manifest_url TEXT NOT NULL UNIQUE,
    firmware_url TEXT NOT NULL,
    firmware_sha256 TEXT NOT NULL,
    firmware_size INTEGER NOT NULL,
    published_at TEXT NOT NULL,
    known_good INTEGER NOT NULL DEFAULT 0,
    imported_at INTEGER NOT NULL,
    approved_by TEXT NOT NULL,
    UNIQUE(version, channel)
);
