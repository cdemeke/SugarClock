# iOS 1.0.0 (34) — Enforced firmware-watch deadline

Fixes Greptile's PR #81 finding that firmware monitoring could wait indefinitely inside saved-clock connection recovery, preventing its three-minute deadline from running and leaving the watch in control of the app.

Each firmware watch now owns an independent 180-second cancellation task. Its lifetime includes connection recovery, settings/status reads, and polling delays. Expiry preserves the existing timeout guidance and cancels the wait. Cleanup releases only the watch's busy ownership and clears its monitor state; ordinary foreground reconnection may continue with Stop, Retry, editing and clock switching available again.

Each deadline belongs to one watch ID. Completion or cancellation retires that deadline, and it cannot cancel a newer watch. Background cancellation does not later produce a timeout message. Expected firmware version evidence remains stored until the clock actually reports that version and successful startup validation. Connection recovery never replays a check/install request.

Validation: 201 Swift tests executed, three opt-in hardware tests skipped, zero failures. The dual-link outage regression failed before the fix and now covers both retryable Bluetooth failures and permanent pairing errors after an accepted install. Other tests cover a stalled settings read, expiry during polling sleep, version-confirmed completion followed by a new watch, background cancellation without a late timeout, released controls, expected-version retention and exactly one submitted install. The complete simulator build and signed iPhone archive passed.

This changes coordination of existing views and timeout text, with no new visual presentation to capture; existing PR screenshots remain representative. TestFlight build 34 targets SugarClock's existing Internal group; processing, testing notes and availability are verified separately. No clock firmware was flashed, and uncommitted app work in the original checkout is untouched.
