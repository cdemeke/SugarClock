# iOS 1.0.0 (33) — Independent Bluetooth and Wi-Fi recovery

Fixes Greptile's PR #81 finding that a Bluetooth pairing error cancelled a saved clock's concurrent Wi-Fi connection and disabled automatic recovery.

Saved clocks now race successful routes rather than ending recovery on the first Bluetooth error. A permanent pairing error retires that Bluetooth attempt while identity-checked Wi-Fi discovery continues. Transient Bluetooth errors keep their existing capped retry backoff independently of the Wi-Fi watcher. A radio powered off during recovery also remains eligible to retry when it returns, including if it reports unavailable while off. Wi-Fi can recover automatically with no remembered address or after DHCP moves the clock, including when the old address belongs to a different device.

Recovery remains foreground-only and cancellable through Stop, backgrounding, or selecting another clock. A new Bluetooth clock without a verified LAN identity uses the existing bounded setup attempts; Bluetooth-only sessions still stop and show pairing advice for permanent errors. Mutations are never replayed by these connection tasks.

Validation: 196 Swift tests executed, three opt-in hardware tests skipped, zero failures. The new missing/stale-address regression reproduced the failure before the fix. Additional production-coordinator tests cover Stop and explicit retry, background/resume, clock switching without late adoption, transient Bluetooth recovery with Wi-Fi unavailable, new-clock retry limits with LAN discovery configured, and a Bluetooth power cycle during recovery. The complete simulator build and signed iPhone archive passed.

This change affects connection coordination and has no new screen layout or visual state to capture. The existing labeled [connection screenshots](screenshots/ios-build28/README.md) and [configuration/diagnostics screenshots](screenshots/ios-build31/README.md) remain representative. TestFlight build 33 targets SugarClock's existing Internal group; Apple processing, testing notes and group availability are verified separately. No physical clock was flashed and no onboarding design is implemented.
