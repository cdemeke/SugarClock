ALTER TABLE devices ADD COLUMN detected_city TEXT NOT NULL DEFAULT '';
ALTER TABLE devices ADD COLUMN detected_region TEXT NOT NULL DEFAULT '';
ALTER TABLE devices ADD COLUMN detected_country_code TEXT NOT NULL DEFAULT '';
ALTER TABLE devices ADD COLUMN detected_location_checked_at INTEGER;
