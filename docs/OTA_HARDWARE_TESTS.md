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

## Update display

- [ ] Manual, automatic, and fleet-managed installs show a continuous `Updating...`
      marquee at 70 ms per pixel, including during verification. Percentage changes
      never reset the scroll and normal screens never alternate with it.
- [ ] Enter an update during a fade or weather animation; the update remains fully
      visible at the configured brightness. Check manual, automatic, and nighttime
      brightness, and confirm the live web display shows the same update message.
- [ ] A centered `BOOT` fits the matrix and is visible for about one second before
      restart. Rendering acknowledgement gates that interval, with a three-second
      total fallback if the main loop is blocked.
- [ ] Interrupt Wi-Fi during a slow firmware transfer. `Update failed` scrolls for
      eight seconds (one full pass takes 7.7 seconds); the normal display and
      polling then resume. Check that a subsequent retry starts a fresh marquee.
- [ ] Background-check failures, bad fleet manifests, authorization/safety deferrals
      (including the final check before flash writes), and boot rollback reports do
      not show the installation-failure message or briefly take over the screen.
- [ ] Navigation/brightness buttons do not interrupt installation or its failure
      message. Middle-button long press still snoozes alerts, even if connection
      information was visible before installation.
- [ ] Alert checks, buzzer, sensors, timers, watchdog and web status remain live.
      Blocking glucose/weather fetches stay paused through the bounded failure
      message and resume afterward; existing OTA safety/rollback checks still pass.
