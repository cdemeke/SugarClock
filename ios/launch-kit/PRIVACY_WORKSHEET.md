# App Privacy and policy worksheet

Prepared from PR #29 code on October 2, 2026. This is a technical inventory and draft-answer guide, not completed App Store Connect answers or a legal determination. Owner review must cover the actual deployed fleet service, hosting logs and support tools.

Apple distinguishes local-only processing from information transmitted off-device and retained for access beyond servicing a request. Include relevant third-party practices and assess data purpose, linkage and tracking separately. A privacy manifest alone does not complete the product-page questionnaire. [Apple: App Privacy Details](https://developer.apple.com/app-store/app-privacy-details/).

## Evidence and disclosure decisions

| Information | Actual implementation / destination | Submission action |
| --- | --- | --- |
| Clock IDs, nicknames, selected device, schema/boot metadata | Phone UserDefaults in `ClockModel.swift` and `SessionPolicy.swift`; not an app analytics upload | Describe local storage; do not infer server collection from local IDs alone. |
| Settings snapshots and pending changes | `LocalSettingsStore.swift`; device-only, when-unlocked Keychain; no iCloud synchronization | State local retention, cancellation/removal behavior and device-unlock requirement. |
| New passwords/tokens/URLs | Explicit pending replacements can be retained in that Keychain workspace until confirmation/discard; Bluetooth sends them to the chosen clock | Do not claim passwords are never stored. Audit provider transmission and support logging; distinguish a user-owned accessory from operator servers. |
| Existing source secrets | BLE `config_public` returns configured indicators, not raw secrets | Claim “configured indicators,” not “the app can recover your saved password.” |
| Bluetooth bond keys | Platform-managed | No custom cryptography or universal PIN. Permission text explains nearby setup. |
| Phone analytics, advertising, tracking, HealthKit, phone location | No relevant SDK/API use found in current app; no native URLSession networking | Current evidence supports no app advertising/tracking. Do not equate this with “the entire SugarClock product collects no data.” |
| Glucose-provider connection | Clock contacts the selected provider over Wi-Fi; the app configures it and reads status | Review the final data flow and provider practices. Do not label health-data handling as absent simply because the phone does not fetch readings directly. |
| Fleet installation ID, firmware, status and feature categories | `src/fleet_manager.cpp` registration/check-in; service persists installation/report records | Determine App Privacy scope for companion-triggered configuration/reporting. If in scope, map persistent ID to Device ID and operational/feature data to the appropriate diagnostics/product-interaction categories and purposes. Review linkage; pseudonymous IDs are not automatically unlinked. |
| Approximate location / IP | Fleet server sees source address; optional IP-based coarse geolocation, see `fleet/README.md` | Owner confirms whether deployed lookup/log retention is enabled. If applicable, declare Coarse Location and relevant other uses. Never say “no location collection” for the complete product without this check. |
| Support messages and attachments | Public issue tracker or future public email | Determine what is retained by the operator/provider, access controls and deletion. Warn against sharing credentials or personal health readings. |
| Marketing-site analytics | Existing `docs/js/analytics.js` uses PostHog; this new local launch page has no analytics | Website policy must cover its actual deployment. Native app and external site are separate surfaces; reassess if embedded web views are added. |

Relevant source files: [phone storage](../SugarClock/Core/LocalSettingsStore.swift), [model](../SugarClock/Core/ClockModel.swift), [manifest](../SugarClock/PrivacyInfo.xcprivacy), [firmware reporting](../../src/fleet_manager.cpp), [fleet details](../../fleet/README.md), [website analytics](../../docs/ANALYTICS.md).

## Retention and deletion already implemented

- A pending secret replacement is cleared after confirmation/discard according to the draft lifecycle. Uncertain saves can stay pending until resolved.
- Removing a saved clock deletes its local Keychain workspace, saved device entry and schema cache. Failure to erase the workspace leaves the clock listed and reports an error.
- Removing an app entry does not erase the clock's NVS credentials or necessarily remove an iOS system bond. A clock bond reset preserves its other settings; a factory reset is different.
- Do not promise that uninstalling clears Keychain storage or that local removal deletes server fleet records.

## Owner facts still needed

Public controller/publisher name and contact; hosting regions and processors; fleet identifiers' retention and backups; server/log IP retention; whether coarse geolocation is enabled; access/deletion request procedure (including how someone proves ownership of a clock); support retention; applicable regional rights and transfer terms; effective date. Do not invent durations or promise a deletion SLA before the service supports it.

The draft [privacy page](site/privacy.html) carries these blockers visibly. Add the finalized public policy link to both App Store Connect and an easily accessible in-app location before submission. The current Help view links the general website but has no dedicated privacy-policy link.

The checked-in manifest declares no tracking, no collected data types, and UserDefaults reason CA92.1. Verify the final archive's aggregated privacy report and align the manifest, final App Privacy answers and actual service behavior. **Do not automatically submit “Data Not Collected.”** The fleet/accessory flow needs an explicit owner determination.
