# OTA hardware acceptance checklist

Automated tests and successful compilation are not substitutes for these physical TC001
tests. Record device serial number, old/new versions, power source, date, and result for each.

- [ ] USB migration from the old layout preserves NVS configuration.
- [ ] Bootstrap-to-newer OTA succeeds and embedded UI assets change with it.
- [ ] Corrupt JSON manifest is rejected.
- [ ] Invalid signature, wrong key, and signed-field modification are rejected.
- [ ] Truncated/corrupt binary and wrong SHA-256 leave the old firmware bootable.
- [ ] Wrong hardware ID and oversized firmware are rejected before writing.
- [ ] WiFi interruption at 10%, 50%, and 90% leaves the old firmware bootable.
- [ ] Power removal during OTA write leaves the old firmware bootable.
- [ ] Power removal after boot-slot selection boots an old valid or complete new image.
- [ ] New-firmware crash before health confirmation rolls back automatically.
- [ ] TLS fails closed when system time is unavailable.
- [ ] Low battery, urgent glucose, urgent notification, buzzer, timer, stopwatch, AP/setup mode,
      and low heap defer installation.
- [ ] Main-loop watchdog remains healthy during a slow download.
- [ ] USB recovery succeeds after a deliberately failed experiment.

No hardware tests are marked complete by the implementation/build process; they must be run
and checked off on an actual Ulanzi TC001.
