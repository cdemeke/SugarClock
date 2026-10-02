# SugarClock App Store submission packet

Draft prepared October 2, 2026 against app 1.0.0 (24) and PR #29. This packet prepares a submission; it does not promise Apple approval or claim medical regulatory clearance. No App Store upload, public page publication, or release is performed by this change.

## What is ready in this PR

- Responsive marketing-page implementation: [site/index.html](site/index.html).
- Support and privacy drafts with visible unresolved owner fields.
- Copyable English metadata: [app-store-metadata.json](app-store-metadata.json).
- Six current iPhone screenshots and six iPad screenshots of real SwiftUI views, using explicit synthetic fixtures. These are design/marketing review assets, not proof of physical pairing.
- Five styled iPhone screenshot panels, social artwork and an existing 1024-pixel icon copy; see [asset guide](README.md).
- Review walkthrough and recording script: [REVIEW_NOTES.md](REVIEW_NOTES.md).
- Evidence-based privacy worksheet: [PRIVACY_WORKSHEET.md](PRIVACY_WORKSHEET.md).

## Submission work and ownership

| Item | Prepared here | Before submission |
| --- | --- | --- |
| App record and signing | Existing bundle `com.sugarclock.companion`, marketing version 1.0.0, build 24 | Owner verifies the actual App Store Connect record/team, agreements, signing and intended distribution regions. No signing credentials belong in Git. |
| Product-page text | Name, subtitle, promotion, keywords and description in JSON | Review claims and localization; fill rights-holder copyright and category. Description makes required hardware explicit. |
| Product URLs | Local marketing, support and privacy HTML | Owner approves final content/contact; publish stable HTTPS URLs and check them logged out. Proposed `/ios/` URLs are placeholders, not live claims. |
| Visuals | iPhone 6.9-inch and iPad 13-inch native captures plus designed panels | Check against the exact submitted Release UI and device set; replace fixtures with sanitized hardware captures where needed. Verify no credentials/personal readings. |
| Review access | Hardware walkthrough and demo-video shot list | Provide a functioning clock with compatible firmware, power and reviewer instructions, or coordinate the review setup with Apple. Record a real phone/clock video. |
| Privacy | Data-flow worksheet and draft policy | Confirm fleet deployment/retention/subprocessors and final App Privacy answers. Publish policy and add an accessible privacy-policy link inside the app. **That in-app link is not implemented by this kit.** |
| Health-related positioning | Companion-configuration description, no measurement/dosing claims | Owner determines applicable medical-device status/clearances and supplies documentation if applicable. A disclaimer alone does not settle classification. |
| Quality | Existing tests/build results linked below | Complete physical BLE pairing/recovery, Wi-Fi failure, retained edits, saved readback, glucose/alerts, OTA/rollback, Dynamic Type and VoiceOver checks on the submitted build. |
| Distribution | Existing authorized TestFlight workflow documented | Choose intended testers, resolve compliance, confirm build is Testing, then verify installation. Public review and release need separate owner direction. |

## Apple fields and current requirements

App name and subtitle allow up to 30 characters each. A privacy-policy URL is required. The app's bundle ID must match the App Store Connect record. [Apple: app information](https://developer.apple.com/help/app-store-connect/reference/app-information/app-information/).

Promotional text allows 170 characters, description 4,000 characters, and keywords 100 bytes. Support URL must provide actual contact information; a repository alone is not a completed support page. Marketing URL is the product-information page. This kit validates the drafted text lengths locally. [Apple: platform version information](https://developer.apple.com/help/app-store-connect/reference/app-information/platform-version-information/).

Apple accepts 1–10 screenshots per screenshot set and prohibits alpha channels. The supplied designed iPhone PNGs are 1320×2868; the iPad exports use the simulator's native 2064×2752 size. The project currently targets both device families (`TARGETED_DEVICE_FAMILY = "1,2"`), so prepare the iPad set too. Recheck the current Media Manager requirements before upload. [Apple: screenshot specifications](https://developer.apple.com/help/app-store-connect/reference/app-information/screenshot-specifications/). App previews are optional; a reviewer demo video is a separate artifact. [Apple: upload previews and screenshots](https://developer.apple.com/help/app-store-connect/manage-app-information/upload-app-previews-and-screenshots/).

For a hardware-dependent app, give reviewers complete access, configuration instructions and a reachable contact; Apple may need the accessory or a demo video. SugarClock's Debug fixture is excluded from Release, so do not tell reviewers a public demo mode exists. [Apple: preparing for App Review](https://developer.apple.com/app-store/review/).

Accurate metadata, a complete working app, privacy access, and substantiated health-related claims matter for review. Do not claim glucose measurement, clinical accuracy, continuous phone monitoring, guaranteed alarms, or regulatory approval. Retain the required-hardware and primary-device wording. [Apple: guidelines 1.4.1, 2.1, 2.3 and 5.1.1](https://developer.apple.com/app-store/review/guidelines/).

## Owner declarations to complete

- **Age rating:** answer the actual content questionnaire, including medical/treatment information accurately; do not assign a guessed age. [Apple: set an age rating](https://developer.apple.com/help/app-store-connect/manage-app-information/set-an-app-age-rating/).
- **Medical-device status:** Apple requires this declaration for certain region/category/content combinations, including Medical or Health & Fitness categories in the EU/EEA, UK and US. Choose the category that represents the product, not one intended to avoid review. Classification and any clearance evidence need owner review. [Apple: regulated medical-device status](https://developer.apple.com/help/app-store-connect/manage-app-information/declare-regulated-medical-device-status/).
- **Export compliance:** the checked-in app uses platform Core Bluetooth/Keychain encryption and declares `ITSAppUsesNonExemptEncryption = NO`. Review the final archive and questionnaire; that flag is not a substitute for the owner's determination. Reevaluate if dependencies change. [Apple: export compliance](https://developer.apple.com/help/app-store-connect/manage-app-information/overview-of-export-compliance/).
- **EU availability:** determine trader status and complete any requested verified contact information before selecting EU storefronts. [Apple: Digital Services Act trader requirements](https://developer.apple.com/help/app-store-connect/manage-compliance-information/manage-european-union-digital-services-act-trader-requirements/).
- **Content rights:** confirm rights to the existing SugarClock icon/artwork and permission for integrations. Do not imply Apple, Ulanzi, Dexcom or Abbott endorsement. Do not use an App Store download badge until there is an authorized live listing; use Apple's unmodified official badge when appropriate.
- **Accounts:** SugarClock has no in-app account creation. Provider credentials are for an existing separate service; do not invent a SugarClock review login. If account creation is added later, revisit in-app account deletion. [Apple: account deletion](https://developer.apple.com/support/offering-account-deletion-in-your-app/).

## Release sequence

1. Close the owner fields and technical gaps above. Compare the final binary with the metadata, privacy worksheet and images.
2. Run the tests/builds and [physical acceptance checklist](../../docs/BLE_ACCEPTANCE.md). Record exact app build and firmware version together.
3. Archive/validate the Release app using the owner's signing configuration. Inspect included permissions, privacy manifest, icons, supported devices and export declaration.
4. With authorization, upload to App Store Connect. Complete metadata, age/category/medical/privacy/export answers and review attachments. Select the intended processed build.
5. Keep the review device/source and relevant services working. Answer review questions with observed behavior; never provide real personal health-account credentials.
6. Prefer manual release. Submit for review and publish only with the owner's explicit direction. TestFlight availability and internal testing are not App Store approval.

The full build and distribution commands remain in [the iOS guide](../README.md). The previous integration results are in [PR29_MAIN_INTEGRATION.md](../../docs/PR29_MAIN_INTEGRATION.md); pending hardware qualification remains pending.
