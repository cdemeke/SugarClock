-- Keyed digest of the network a device first enrolled from. Bounds how many
-- enrollments one network can hold; never stores the address itself.
ALTER TABLE devices ADD COLUMN registration_source TEXT;
CREATE INDEX devices_registration_source ON devices(registration_source);
