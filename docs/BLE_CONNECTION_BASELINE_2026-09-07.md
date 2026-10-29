# Physical Bluetooth baseline — September 7, 2026

## Setup and user outcome

Existing TC001 firmware reporting 0.3.0, latest available iOS TestFlight build 7 requested for the test, clock connected over USB. Capture started at 2026-09-07T14:52:09.293+00:00 and ended after 2026-09-07T15:02:32.179+00:00 (approximately 10.4 minutes). A boot was observed when the serial connection opened; startup samples are not steady-state measurements. The running firmware's exact hash and phone build number were not independently read back during this session.

The owner reported approximately 30 seconds of Loading settings, then Connected alternating with Reconnecting quietly. When trying to change the pet companion, the app reached Couldn't connect and showed Last synced around 10:57 AM Eastern. The owner confirmed Save changes was disabled, so no pet-setting save was submitted. Brightness/save and background/reopen tests were not completed because foreground reconnection failed first.

## Recorded evidence

- 13 network-radio pause messages, including startup and an incomplete final cycle when logging ended.
- 18 occurrences of Phone authenticated followed by Phone connected as the next BLE event. Several were followed by local-host disconnection with network=0.
- Example: authentication at 14:58:38.787 UTC, connected callback at 14:58:39.117, disconnection at 14:58:40.291 (reason 534, network=0).
- Sample recent pause-to-BLE-initialization intervals: 3.52, 3.69, 6.09, 6.71, and 6.57 seconds. BLE initialization logging precedes advertising start and does not establish that the app loaded settings successfully.
- Latest recorded minimum heap: 24,180 bytes. No crash or allocation-failure keywords were captured. This is a short filtered capture, not a memory-safety or overnight qualification.
- Internet reading receipt events continued during the observation. Actual readings, credentials and passkeys were omitted from the capture.

## Isolated reproduction and likely cause

A temporary C++ diagnostic extracted the actual onConnect, onAuthenticationComplete and allowed functions from src/ble_manager.cpp, using an authenticated, encrypted, bonded peer with a 16-byte key. Normal connect-then-authenticate ordering allowed requests. The observed authenticate-then-connect ordering rejected requests: onConnect unconditionally resets secure=false after authentication has already set it true. onRead/onWrite disconnect when allowed fails.

This reproduces the application-state failure for the observed ordering; the diagnostic uses stubbed Bluetooth peer information and is not a replacement for a physical candidate test. The existing serial messages lack connection handles and call-site rejection reasons, so this remains a strongly supported cause rather than proof of every observed disconnect.

The pinned NimBLE source in .pio/libdeps/esp32dev/NimBLE-Arduino/src/nimble/nimble/host/src/ble_gap.c delays its connected event until remote feature/version exchange completes. Firmware must handle current verified peer security without assuming that connected always precedes authentication.

## Recommended next candidate

First correct the callback-order handling while retaining all encryption, authentication, bond and key-length checks. Add regressions for both event orders, unauthenticated peers, disconnect/reconnect and old-session cleanup. Include privacy-safe connection/rejection diagnostics for physical confirmation. Confidence is high in the isolated defect; improvement on the clock remains to be measured.

Then repeat foreground observation for five minutes, one pet/brightness save with visible clock change and app readback confirmation, and lock/reopen. Only after reliable reconnection is established should a separate memory/radio-pause optimization be evaluated. Scheduled network pauses remain a separate issue.

No production code or firmware image was changed for this baseline. Private local filtered logs, phase markers and diagnostic are under /tmp/sugarclock-baseline-20260907. Logging was stopped after reproducing the failure.

## Correction after owner authorization

The owner subsequently asked to fix the connection bug. The firmware now derives authorization from the current peer's encryption, authentication, bond, Secure Connections and 16-byte key state. Both connection and successful-authentication callbacks can establish a session, but the session is initialized only once per live handle. A delayed connection callback therefore preserves authentication and any request already transferring. Disconnects for other handles cannot clear the current session, and rejected authentication targets the peer that failed. Diagnostics include connection handles and fixed rejection reasons without peer addresses, passkeys or payloads.

The actual production callback code is exercised by a host regression harness. Coverage includes both event orders, preservation of a pending request/response, rejection of a different peer, disconnect cleanup, handle reuse without inherited authorization, all five security requirements, admission windows and security loss. The regression fails against the original implementation and passes with the correction. All **49 repository Python tests pass**.

The pinned firmware build and OTA slot-size check pass: binary **1,586,672 bytes**, SHA-256 `dc7bf1e745a1e6bcaa9215220b7f184e8c9b0aa3a75eead657c307585db70824`, leaving **248,336 bytes** in the existing application slot. Static RAM remains 93,512 bytes. The iOS app, three-attempt retry limit and network-radio pause policy were not changed for this correction.

A fresh private 4 MiB device backup was captured before installation. Its partition table matches the build, and its application at `0x10000` matches the previously documented corrective image hash `cbf48a9ffe69ef9fca581755190be24fb4c7748a104b80d4445a12902d8c878a`, establishing the baseline image identity that was not available from serial version text alone. Backup and installation evidence are stored privately under `/tmp/sugarclock-session-fix-20260907` and excluded from the repository.

The application-only write at `0x10000` completed with the device hash verified. A subsequent read of the first 64 KiB matched the pre-flash backup byte for byte, confirming preservation of bootloader, partition table, NVS (including settings and bonds) and OTA metadata before restarting. LittleFS and the other application slot were outside the write range.

After restart at 15:34:35 UTC, setup completed, saved Wi-Fi connected, provider login succeeded and a reading was received. BLE resumed after startup network work. The owner was asked to tap Retry in the existing iOS app and test foreground reconnection and Save availability; that phone-side result remains pending.

The 15-minute capture ended at approximately 15:49:35 UTC with no phone connection events recorded. The owner later reported that the first save succeeded but a second update after returning to the app showed an unconfirmed-save message; whether the second change reached the clock was unknown. That operation was not captured, and the USB device was no longer present when checking at 18:12 UTC. This does not establish that every connection or save-confirmation issue is resolved. Two isolated iOS checks (second save after background return, and recovery after losing its acknowledgment) passed; a recorded physical repeat remains necessary.
