# OTA first-boot validation

Arduino-ESP32 calls `initArduino()` before SugarClock's `setup()`. In the pinned
framework, the weak `verifyRollbackLater()` hook defaults to false and
`verifyOta()` defaults to true. This accepts a pending image before SugarClock
can inspect it or run its local health checks. Enabling rollback in the SDK and
bootloader alone is insufficient.

`src/ota_boot_validation.cpp` supplies a strong C-linkage
`verifyRollbackLater()` implementation that returns true. SugarClock's existing
OTA manager then retains responsibility for acceptance: at least 15 seconds,
loaded configuration, available web assets, more than 100 loop iterations, and
at least 55,000 bytes of free heap. Network availability is not a requirement.
An image that restarts before acceptance remains eligible for bootloader
rollback. No bootloader change is required on the tested TC001.

The host regression test links the actual application source against a weak C
default and a C caller. It catches both removal of the override and accidental
C++ name mangling, which would otherwise silently preserve early acceptance.

## Scope of hardware validation

The physical test stages a new image and `NEW` OTA selection metadata over USB,
retaining a valid previous image in the other slot. This isolates first-boot
behavior using the installed bootloader. It does not repeat an OTA download,
exercise fleet authorization/result acknowledgments, or simulate full power
removal. Test fixtures that restart before health confirmation are private and
are not included in the production firmware.

The firmware version is advanced to 0.2.13 to distinguish this fix from the
existing 0.2.12 test image. A locally deployed canary is not a published signed
release. Public GitHub Latest must remain unchanged until the release workflow
and remaining migration acceptance checks are complete.

## Physical results — September 16, 2026

A TC001 with the original installed bootloader and a valid v0.2.12 fallback
passed both first-boot tests:

- Healthy v0.2.13: API observations remained pending at approximately 5 and 11
  seconds, then became valid at approximately 16 seconds. Serial output logged
  both pending validation and successful local validation; metadata readback
  confirmed `NEW → PENDING_VERIFY → VALID`.
- Deliberate restart before SugarClock setup/health confirmation: the test image
  logged `PENDING_VERIFY` (state 1), restarted once, and the installed bootloader
  selected the original v0.2.12 slot. Metadata readback showed the failing slot
  `ABORTED` (state 4), with the fallback still `VALID` (state 2).

The production binary excludes the failure fixture. Its SHA-256 is
`7a1e232aa33b67de4bcd3ec87b7247aef6ea7974fbeb348d806f63b1f13150e7`;
a clean rebuild after removing the fixture produced the same bytes.
The 117 Python tests, 22 JavaScript tests, firmware and filesystem builds, and
layout/OTA stack-budget checks passed locally.

These results replace the previous inference that `pending_verification=false`
alone established successful health validation. A historical Fleet
`boot_validated` value is also insufficient evidence for a new test run. The
first-boot transition and rollback were observed directly here.
