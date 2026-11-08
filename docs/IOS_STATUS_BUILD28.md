# iOS build 28 — shorter connection messages

This iteration continues the previously delivered Wi-Fi connection work from TestFlight builds 25–27. Those changes were still uncommitted in the earlier workspace; they are carried forward on top of the current `iOS` branch without replacing its newer firmware/update and pairing-help corrections.

The My Clocks status line now uses **Not connected**, **Searching…**, **Connecting…**, or **Updating settings…**. A ready session says **Connected via Wi-Fi** or **Connected via Bluetooth**. Other clocks that answer over Wi-Fi retain **Available on Wi-Fi**. Firmware updates still say **Updating clock…**. Waiting/sending controls for an explicit settings update use **Connecting…** and **Updating settings…**.

Failure status badges and offline-settings notices say **Not connected**. The badge's details retain the underlying error and retry guidance. This iteration changes presentation, not connection retries, settings persistence or save confirmation.

## Validation

- Swift package suite: 180 tests executed, 3 opt-in physical-clock tests skipped, 0 failures.
- Complete Debug iPhone simulator build and signed Release iPhone archive succeeded with Xcode 27.2.
- Archive metadata: `com.sugarclock.companion`, version `1.0.0`, build `28`; local-network permission description present; exempt-encryption declaration remains false.
- Actual simulator screenshots verify the three changed list states. See [gallery](screenshots/ios-build28/README.md). These use clearly labeled sample data and do not establish physical radio behavior.
- TestFlight **1.0.0 (28)** is verified **VALID / IN_BETA_TESTING** in the existing **Internal** group with exact-build membership and matching test notes on November 1, 2026. Availability does not establish installation on the owner’s phone.

## Onboarding exploration

The requested onboarding concepts remain separate review artifacts. No onboarding flow, installer handoff, firmware behavior or new permissions are implemented in build 28.

The existing BLE-capable companion firmware supports pairing and Wi-Fi trials, so a newly flashed compatible clock can be configured from the phone. Mainline firmware without BLE must first be reachable over Wi-Fi; the app cannot provision an offline non-BLE clock through Bluetooth. The eventual setup should detect this distinction, keep configured values, request system permissions at the relevant action, and check Wi-Fi, internet and fresh glucose separately.
