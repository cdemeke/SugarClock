# iOS 1.0.0 (31) — Saved network details and persistence diagnostics

Addresses the two Greptile P2 findings on PR #81: the Wi-Fi adapter discarded saved network details, and diagnostics always showed settings persistence as unconfirmed.

The HTTP settings adapter now retains SSID, security type, EAP method, identity, anonymous identity, and CA-validation preference for the dedicated Wi-Fi editor. Personal and enterprise password presence becomes a configured flag; password values never enter returned settings. These fields remain excluded from the ordinary editable schema and `settings.patch`, preserving the separate Wi-Fi trial flow.

Each HTTP status refresh reads fresh `GET /api/config` durability evidence. Diagnostics reports persistence only for an actual JSON Boolean `configuration_saved: true`. False, absent, malformed, or unreadable evidence remains unconfirmed; a previous confirmed read is never reused. Failure to read this optional evidence leaves the other diagnostics and OTA monitoring available. Older firmware without the marker still shows Unconfirmed, as in build 30.

Validation: 190 Swift tests executed, three opt-in hardware tests skipped, zero failures. Both new regressions failed before the adapter fixes and passed afterward. Coverage includes personal/open and enterprise readback, secret redaction, exclusion from ordinary patches, changing durability evidence, unavailable reads, and the production coordinator's save/reconciliation flow. The complete simulator build and signed iPhone archive passed.

[Current simulator screenshots](screenshots/ios-build31/README.md) show production views with explicitly labeled sample data. Network settings samples pass through the production HTTP settings mapper; diagnostics uses sample status. These captures verify presentation, while the client/coordinator tests verify network behavior. No physical clock was flashed. TestFlight build 31 targets SugarClock's existing Internal group; availability is verified separately after Apple processing.
