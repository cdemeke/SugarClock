# OTA update-task memory fix: hardware validation

Tested September 13, 2026 on one USB-connected Ulanzi TC001 using the live
Fleet service. This is a targeted preview test, not fleet-wide acceptance.

## Failure and correction

The previous updater overflowed its 12,288-byte task stack while checking an
update, before download authorization. The compiler reported a 6,224-byte
`ota_worker` frame: inlining reserved space for the 4 KiB download buffer and
manifest even during earlier TLS operations.

Commit `5bfa3032cfd58e0c2906233eca82c3c15418a710` moves those allocations to
checked, scoped heap storage. The update helper returns before task deletion
so C++ cleanup runs on failure paths. The task retains its original stack
allocation. A compiler-report check now rejects OTA source frames larger than
2,048 bytes, unbounded dynamic frames, or a missing worker report.

After the fix, the largest reported OTA source frame is 1,248 bytes. The
successful physical update logged 4,936 bytes of minimum free task stack.
The compiler check measures individual source frames; it does not replace
runtime testing of nested TLS calls.

## Results

The clock was first bootstrapped over USB with the fixed updater reporting
v0.2.7. Both OTA attempts used the signed `preview-v0.2.9-1` release built
from the commit above. Its firmware image is 1,475,808 bytes.

| Test | Observation | Result |
| --- | --- | --- |
| Interrupted transfer | Reset the clock through USB at 14% download, after authorization. It rebooted v0.2.7 from the original slot, reconnected, and received glucose data. Fleet recorded `failed` with reason `interrupted`. | Passed |
| Successful retry | Retry created a fresh attempt without inherited authorization. The clock obtained new authorization, downloaded the signed image, booted v0.2.9 in the other slot, and Fleet recorded `boot_validated`. Glucose data resumed. | Passed |
| Pause after authorization | Paused the rollout at 12% during the retry. The already-authorized transfer continued and completed successfully. | Passed |
| Configuration and scope | Original saved settings and automatic-update schedule restored. Other eight registered clocks' version, credential, and nickname records unchanged; no rollout targets created for them. | Passed |
| Release isolation | Preview candidate left paused. GitHub public Latest remains v0.2.6, and the release guard confirms its baseline is unchanged. | Passed |

All 103 Python tests passed. Firmware and filesystem builds, partition/layout
validation, compiler stack-frame validation, and the signed preview workflow
passed. The stack checker includes regression coverage for the original
6,224-byte frame.

Raw device logs, configuration snapshots, and database evidence are retained
locally and are excluded from this repository because they can contain private
device and account information.

## Limits

The interruption was a hardware reset during download, not complete power
removal: this clock has an internal battery. Full power-loss/brownout recovery,
interruption after boot-slot selection, and a deliberately crashing new image's
automatic rollback still require the separate hardware acceptance tests in
[OTA_HARDWARE_TESTS.md](OTA_HARDWARE_TESTS.md). This test does not establish
stack headroom for every possible network condition.
