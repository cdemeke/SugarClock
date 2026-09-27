-- Pending legacy registrations never had permission to check in. Preserve their
-- identity and nickname without inventing a check-in timestamp or active usage.
-- This also protects surviving installations from an already-applied 0006.
ALTER TABLE devices ADD COLUMN registration_cleanup_exempt INTEGER NOT NULL DEFAULT 0;
UPDATE devices SET registration_cleanup_exempt=1;
