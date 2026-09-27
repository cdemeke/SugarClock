ALTER TABLE devices ADD COLUMN blocked_at INTEGER;
ALTER TABLE devices ADD COLUMN last_checkin_at INTEGER;
ALTER TABLE devices ADD COLUMN capabilities_json TEXT NOT NULL DEFAULT '[]';
ALTER TABLE devices ADD COLUMN features_json TEXT;
ALTER TABLE devices ADD COLUMN features_reported_at INTEGER;
UPDATE devices SET verification_state='verified' WHERE retired_at IS NULL;
UPDATE devices SET last_checkin_at=last_seen WHERE uptime_seconds IS NOT NULL;
CREATE INDEX devices_activity ON devices(last_checkin_at);
CREATE TABLE blocked_identities (installation_id TEXT PRIMARY KEY, blocked_at INTEGER NOT NULL);
CREATE TABLE registration_limits (source TEXT PRIMARY KEY, window_start INTEGER NOT NULL, attempts INTEGER NOT NULL);
CREATE TABLE daily_metrics (day TEXT PRIMARY KEY, captured_at INTEGER NOT NULL, counts_json TEXT NOT NULL);
CREATE TABLE rollouts (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 release_id INTEGER NOT NULL REFERENCES releases(id),
 kind TEXT NOT NULL CHECK(kind IN ('candidate','stable')),
 percentage INTEGER NOT NULL CHECK(percentage BETWEEN 1 AND 100),
 paused INTEGER NOT NULL DEFAULT 0,
 ended_at INTEGER,
 previous_stable_id INTEGER REFERENCES rollouts(id),
 created_at INTEGER NOT NULL,
 administrator TEXT NOT NULL
);
CREATE TABLE rollout_cohort (
 rollout_id INTEGER NOT NULL REFERENCES rollouts(id) ON DELETE CASCADE,
 installation_id TEXT NOT NULL,
 rank INTEGER NOT NULL,
 PRIMARY KEY(rollout_id,installation_id)
);
CREATE TABLE rollout_targets (
 id TEXT PRIMARY KEY,
 rollout_id INTEGER NOT NULL REFERENCES rollouts(id) ON DELETE CASCADE,
 installation_id TEXT NOT NULL,
 status TEXT NOT NULL DEFAULT 'targeted',
 reason TEXT,
 offered_at INTEGER,
 authorized_at INTEGER,
 updated_at INTEGER NOT NULL,
 UNIQUE(rollout_id,installation_id)
);
CREATE INDEX rollout_targets_installation ON rollout_targets(installation_id,rollout_id);

ALTER TABLE releases ADD COLUMN hardware TEXT NOT NULL DEFAULT 'ulanzi-tc001-esp32-4mb';
ALTER TABLE releases ADD COLUMN minimum_ota_version TEXT NOT NULL DEFAULT '0.0.0';

UPDATE commands SET status='expired' WHERE type IN ('ota_install','ota_rollback_previous','set_channel') AND status IN ('queued','delivered','deferred','accepted');
UPDATE devices SET detected_region='';

ALTER TABLE releases ADD COLUMN metadata_complete INTEGER NOT NULL DEFAULT 1;
UPDATE releases SET metadata_complete=0;
