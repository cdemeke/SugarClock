# App Review notes and real-device recording plan

Use this text in App Store Connect only after filling the bracketed fields and completing the described checks. Do not submit unresolved placeholders. A screenshot fixture is not a reviewer-access mechanism in the Release build.

## Copyable review notes

SugarClock is a configuration companion for a Ulanzi TC001 running SugarClock Bluetooth-capable firmware. The app does not measure glucose or calculate insulin/treatment doses. It configures a separate companion display; primary glucose devices and their alarms remain necessary.

No SugarClock sign-in is required. Bluetooth permission is used to find and configure the clock. The app does not use background Bluetooth, HealthKit or phone location. The clock separately needs Wi-Fi to fetch provider readings and firmware updates.

Review build: [APP VERSION AND BUILD]
Clock firmware: [EXACT VERSION / ARTIFACT HASH]
Review contact: [NAME, EMAIL, PHONE AND TIME ZONE]
Hardware provision / agreed access arrangement: [DETAILS]
Real-device demonstration video: [ACCESSIBLE VIDEO URL OR ATTACHMENT]

1. Power the provided clock near the iPhone/iPad. Open SugarClock and allow Bluetooth.
2. Hold only the clock's middle button for three seconds, then release. Open My Clocks → Add clock, select its advertised name and enter the clock's six-digit code in the system pairing prompt. The code is fresh; there is no fixed review PIN.
3. Open the saved clock. Existing configuration loads without requiring saved passwords to be re-entered.
4. In Display, change brightness. Review the pending change, then choose Update clock. Observe the clock brightness and the app's confirmed completion. Reopen Display and verify the saved value.
5. Choose Pixel Pets and select another companion on the supplied compatible firmware. Update the clock and observe its pet screen. The clock may cycle through enabled screens.
6. Make edits on multiple settings screens. Open Review; use an X to remove one change, then update the remaining changes. Keep the app foreground while an update waits for reconnection.
7. Background and return to the app. The clock continues independently; the foreground app reconnects. A clock network request can briefly interrupt Bluetooth.
8. For source testing, use the clock's explicitly labeled Demo (synthetic data) source or a dedicated test source supplied for review. Synthetic readings demonstrate display behavior only. Restore the intended source afterward. Do not use a real person's health account.

Firmware updates request the existing signed Wi-Fi update path. Firmware bytes are not transferred over Bluetooth, arbitrary update URLs are not accepted, and the app does not download executable iOS code. A controlled update may need a specifically authorized firmware release; do not promise an available OTA update if none is assigned to the review clock.

If pairing is stale, hold the middle button for ten seconds and release to reset all clock Bluetooth bonds; forget the old device in iOS Settings if present, then pair again. Wi-Fi/source/display settings remain intact. Do not use factory reset unless intentionally clearing the clock configuration.

## Record a 2–3 minute uncut demonstration

| Segment | Capture | What it establishes |
| --- | --- | --- |
| 0:00–0:20 | Phone app build, clock firmware, both devices in frame | Exact hardware/software under review |
| 0:20–0:50 | Physical gesture, code display and iOS pairing prompt | Real authenticated pairing, not a mock transport |
| 0:50–1:20 | Brightness edit → pending review → Update clock → visible clock change/readback | Persistence and feedback |
| 1:20–1:45 | Pixel Pets grid and selected pet on the display | Screenshoted feature exists on hardware |
| 1:45–2:10 | Two edits, remove one with X, update remaining edit | Draft review and cancellation |
| 2:10–2:40 | Background/foreground and recovery; clock keeps running | Independent operation and foreground session behavior |
| 2:40–3:00 | Provider status or clearly labeled synthetic source | Honest distinction between connection and data retrieval |

Use non-personal test data; hide account credentials and local network identifiers. If an error occurs, retain the evidence and fix/retest rather than editing it into a success claim. Test at least one iPad layout because the build targets iPad as well as iPhone. This reviewer recording has **not** been performed by this PR change.
