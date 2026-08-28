CREATE TABLE fleet_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
INSERT INTO fleet_settings(key, value) VALUES ('enrollment_until', '0');
ALTER TABLE devices DROP COLUMN reported_nickname;
ALTER TABLE devices DROP COLUMN reported_location;
