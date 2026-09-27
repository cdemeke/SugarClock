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
If only heap is low, the image stays pending until heap recovers or 60 seconds
have elapsed since validation started. Recovery permits immediate acceptance
after the initial 15-second observation period; persistent low heap at the
deadline requests rollback. Missing configuration/assets or insufficient loop
iterations still fail at the initial check. A failed SDK acceptance call, or a
rollback call that returns without rebooting, is retried no sooner than five
seconds later. The image stays pending while those calls fail.
An image that restarts before acceptance remains eligible for bootloader
rollback. No bootloader change is required on the tested TC001.

The host regression test links the actual application source against a weak C
default and a C caller. It catches both removal of the override and accidental
C++ name mangling, which would otherwise silently preserve early acceptance.
Host C/C++ compilers are required: a missing compiler fails the test rather than
silently skipping this protection. Policy tests compile the production decision
code and cover heap recovery, the deadline, failed-attempt backoff, and counter
wraparound. After a firmware build, `scripts/check_layout.py` also checks the
installed Arduino weak-hook signature and initialization call, plus the live
linker-map definition's ownership by `src/ota_boot_validation.cpp.o`. A platform
upgrade that changes these assumptions must be reviewed explicitly.

## Restarts while validation is pending

Avoid manual restart, factory reset, or removing power until
`pending_verification` becomes false. These actions can make the bootloader
reject even a healthy image and return to its valid fallback. The usual window
is about 15 seconds, but low heap extends it to 60 seconds and SDK failures can
extend it further. Restart endpoints keep their existing behavior; they are
not blocked during validation. The 30-second initial Fleet check-in delay also
does not exclude remote restart commands during an extended window.

When Fleet records `rolled_back`, that target is terminal for the rollout and
is not automatically offered the release again. The existing **Retry failed**
action retries only `failed` targets, not `rolled_back` targets. Investigate the
restart, then create a new rollout/candidate selection if another attempt is
appropriate (finish the existing candidate selection first if applicable).

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

At commit `00ce9a94458a649985b6d2ab87b730bbb807e084`, a TC001 with the original
installed bootloader and a valid v0.2.12 fallback
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
rebuilding production source after removing the fixture produced the same bytes.
The 117 Python tests, 22 JavaScript tests, firmware and filesystem builds, and
layout/OTA stack-budget checks passed locally.

These physical results precede the review changes that add heap grace and SDK
retry backoff. Those changes have host regression coverage but need a fresh
hardware test of the final release candidate before public promotion. The
previous binary's hash and physical observations above do not identify or
validate the revised binary.

These results replace the previous inference that `pending_verification=false`
alone established successful health validation. A historical Fleet
`boot_validated` value is also insufficient evidence for a new test run. The
first-boot transition and rollback were observed directly here.
