# iOS companion review follow-up — November 1, 2026

This follow-up addresses the three unresolved inline discussions on PR #29 and two additional findings in its earlier conversation reviews. PR #81 had no review comments when inspected; it receives these fixes through its `iOS` base.

- **Weather test responsiveness:** both forced tests and periodic weather fetches run on a FreeRTOS worker. The main loop retains ownership of rendering/buttons/alerts and the existing network lease still serializes HTTPS. The worker uses the exact configuration snapshot admitted by the scheduler. Completed weather data, diagnostics and test generation publish together; no reader receives a worker-owned reference. Worker creation failure releases the lease and completes a queued test as failed.
- **Settings recovery:** after a redo journal is accepted, its configuration becomes the rollback point even if legacy-key mirroring or journal cleanup fails. Rejecting the next journal restores that recoverable configuration, matching the next boot, while durability remains false until the transaction finishes.
- **App page analytics:** the companion marketing page loads the same deferred configuration/bootstrap scripts, in the same order, as Home and FAQ. Existing host restrictions and URL sanitization are unchanged.
- **Glucose source changes:** published readings, history, overrides and reachability metadata are invalidated immediately under the publication lock. A running old-source worker retains its private state until it finishes; it cannot republish the obsolete reading. Its completion is discarded before the next source initializes.
- **Discarded secret replacements:** the per-field X action removes the replacement from both the editable draft and uncertain submission snapshot. It retains nonsecret submission/receipt evidence and unrelated fields, never replays a mutation, and restores the in-memory draft if secure storage cannot persist the discard.

## Validation

- PR #29: 153 Swift tests passed, including two secure-discard/relaunch/storage-failure regressions.
- 177 Python/compiled-host tests passed. The three new production-boundary harnesses cover repeated persistence failures, blocked weather workers/lease contention/task creation failure, and immediate glucose invalidation while a fetch remains blocked.
- On this ARM Mac, the migration test used the existing native installer tool through the new optional `SUGARCLOCK_MKLITTLEFS` test override. PlatformIO's installed Intel binary cannot run here. No global tool installation was modified.
- 11 site analytics tests and launch-kit metadata/assets verification passed.
- Normal ESP32 firmware build and layout/OTA stack guards passed: binary 1,753,344 bytes in the unchanged 1,835,008-byte slot. The build does not establish physical worker stack/heap or RF reliability.
- These changes preserve the existing app layout. Existing screenshots remain representative; no onboarding design is implemented.

The firmware is built for validation only; no physical clock is flashed and no public firmware release is published. The combined PR #81 app iteration is distributed separately with a new TestFlight build number.

Reproduce the complete host suite on this ARM Mac:

```sh
SUGARCLOCK_MKLITTLEFS="$PWD/onboarding/TC001Setup/TC001Setup/Resources/Tools/mklittlefs" \
  python3 -m unittest discover -s tests -v
```
