# SugarClock for iOS

Native SwiftUI + Core Bluetooth companion for firmware 0.3.0 and protocol 1. Minimum deployment target: **iOS 17**, chosen for modern SwiftUI navigation/accessibility without third-party dependencies. Foreground configuration only; the clock works independently when the app closes. No cloud account, subscription, backend, custom pairing cryptography, or background Bluetooth mode.

## Build and run

Open `ios/SugarClock.xcodeproj`, choose the SugarClock scheme and an iPhone. Select your own Apple development team and a bundle identifier you control in Signing & Capabilities. No team ID or credentials are committed. Enable Developer Mode on a development phone when iOS requests it, then build/run. Pair through the app; iOS shows the system passkey prompt on first authenticated GATT access.

```sh
swift test --package-path ios
xcodebuild -project ios/SugarClock.xcodeproj -scheme SugarClock \
  -destination 'generic/platform=iOS' -configuration Release \
  CODE_SIGNING_ALLOWED=NO build
xcodebuild -project ios/SugarClock.xcodeproj -target SugarClock \
  -sdk iphonesimulator -configuration Debug CODE_SIGNING_ALLOWED=NO build
```

This machine's current Xcode installation has mismatched CoreSimulator components. Source compilation works, but full asset-catalog packaging requires an administrator to repair/install the matching Xcode components and runtime. For an unsigned development build of the current source while that is pending:

```sh
xcodebuild -project ios/SugarClock.xcodeproj -target SugarClock -sdk iphoneos \
  -configuration Release CODE_SIGNING_ALLOWED=NO \
  EXCLUDED_SOURCE_FILE_NAMES=Assets.xcassets build
```

This explicitly omits branding assets; it is not the distribution build and does not establish radio reliability. The normal project retains its assets and privacy manifest. `generate_project.py` deterministically regenerates the checked-in project after adding Swift source files/assets; XcodeGen is not required.

`Core/Wire.swift` contains encoding, bounded reassembly, the transport contract, request sequencing and saved-ack validation. `Core/BluetoothTransport.swift` is the actual Core Bluetooth implementation. It never falls back to a mock. `MockClockTransport` is explicitly opt-in and compiled only for debug previews; `ProtocolPreview` is labeled as a mock. Swift Package tests exercise the protocol and transport contract without a radio. `App/ClockModel.swift` coordinates the UI, identity checks and update reconnection. Credentials typed into editors stay in process memory only and are cleared from editor state after submission/disappearance. Preferences store only clock IDs, nicknames and expected firmware versions. Platform bonding owns its keys; no app-owned secret currently needs Keychain.

The system Bluetooth usage explanation is generated into Info.plist. `PrivacyInfo.xcprivacy` declares app-local UserDefaults use with CA92.1, matching [Apple’s required-reason definition](https://developer.apple.com/documentation/bundleresources/app-privacy-configuration/nsprivacyaccessedapitypes/nsprivacyaccessedapitypereasons). The app uses native Forms, labels, navigation, progress/error text, flexible layout, SF Symbols and existing SugarClock branding. Dynamic Type, VoiceOver and dark-mode physical visual checks are listed in the acceptance document. Wi-Fi and glucose-provider statuses are deliberately separate from configuration persistence.

## Testing on a clock

Use [BLE_MIGRATION.md](../docs/BLE_MIGRATION.md) for fresh, configured-OTA, legacy-USB and broken-Wi-Fi paths. A configured clock requires holding only the middle button for three seconds then releasing to admit a new phone. Holding only the middle button for ten seconds then releasing resets all bonds; also forget the device in iOS Settings when recovering stale bonds. This preserves clock configuration.

After connecting, open Clock Settings. Existing values and secret-configured indicators are read from the clock. Each editor sends only the field being changed. Secrets have separate leave/replace/clear actions. Integer ranges come from firmware metadata. Glucose thresholds remain integer mg/dL in the protocol; mmol conversion occurs only on an explicit edit/save. Pairing and routine settings changes require neither a known IP address nor joining the temporary AP.

Enterprise PEAP/TTLS settings and an existing CA are preserved/supported. This protocol advertises certificate preservation/use, not CA upload: a new certificate is still uploaded through the existing web settings. That capability boundary is shown in the app.

## TestFlight preparation (no upload performed)

1. Complete [physical acceptance](../docs/BLE_ACCEPTANCE.md), including real passkey security, memory pressure, reboot/reconnect, rollback and migration checks.
2. Use an Apple Developer Program account you control. Create the app identifier and App Store Connect app record with your chosen bundle ID, name and SKU. Configure a real development/distribution team in Xcode; do not reuse a placeholder team ID.
3. Review the reused app icon for App Store icon requirements, set the desired marketing version/build number, and prepare screenshots, support URL, privacy policy and reviewer instructions explaining the required TC001 hardware and pairing gesture.
4. Complete App Privacy and export-compliance answers based on the actual final build, including standard BLE encryption and any existing firmware network behavior. The phone app has no analytics SDK or cloud account. Do not invent regulatory or compliance answers.
5. Archive the Release scheme for a generic iOS device using your signing configuration. Validate the archive in Organizer. A successful unsigned SDK build is not a signed install/archive validation.
6. Only after explicit authorization, upload to App Store Connect, finish any required beta review, and add the intended testers. This task does not archive for distribution, upload, publish a firmware release, or flash clocks.

See [verification results](../docs/BLE_VERIFICATION.md) for exact local builds/tests and remaining environment requirements.

## Screenshots

See the [screenshot gallery](../docs/screenshots/README.md) for real simulator captures of the production views with clearly labeled sample data. `capture_screenshots.py` and Debug-only `ScreenshotPreview.swift` provide reproducible captures with Bluetooth and interaction disabled. Screenshot mode requires the explicit `SUGARCLOCK_SCREENSHOT` environment variable; it is absent from Release and never substitutes for a failed connection.

The native [design system and asset provenance](DESIGN.md) follow web UI PR #30, with grouped settings cards, inline controls, and matching system light/dark appearance.

On the no-PSRAM TC001, firmware can briefly suspend Bluetooth to free memory for glucose or other scheduled TLS requests. The current app automatically attempts bounded foreground reconnection, verifies device identity and refreshes settings. It retains unsaved drafts and never replays a settings mutation automatically. Rebuild/run the current app source to include this behavior; older development app builds may require tapping the saved clock again.


### Connection timeout correction (2026-09-06)

Install a newly built iOS app as well as the latest companion firmware for this correction. The previous development app disconnected on `.inactive`, which can occur while iOS presents its Bluetooth pairing sheet. It now disconnects only on `.background`. Firmware also protects fragmented reads and initial authentication before a bounded network pause; it still suspends BLE for TLS/OTA memory and does not promise a continuous radio connection.

During reconnect, settings remain readable/editable, device commands wait for readiness, and Stop reconnecting / Reconnect controls remain accessible. An interrupted schema load is retried as a complete read; existing drafts are preserved. After installing, test pairing (or bond reconnection), full settings load, brightness save/readback, remaining foreground through at least two glucose polls, and a background/foreground round trip. No connected iPhone was available to the agent for signed installation or these checks.
