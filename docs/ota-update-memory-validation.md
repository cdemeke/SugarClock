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
2,048 bytes, unbounded dynamic frames, or missing required reports/functions.

After the fix, the largest reported `ota_manager.cpp` frame is 1,248 bytes. The
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

## Review follow-up

The stack check now requires reports for `ota_manager.cpp`, `ota_manifest.cpp`,
and `fleet_manager.cpp`, including the worker, signature-verification, and
authorization functions. Every frame in those three reports is checked, including
helpers. Their largest frames are 1,248, 1,376, and 800 bytes respectively.
This is a conservative source-file budget: `fleet_manager.cpp` also contains
functions used only by the separate Fleet task, which has a 14,336-byte stack.
An oversized frame there still fails this check; the diagnostic identifies the
source file and function without claiming it runs on the OTA task.
A local layout check explicitly reports a skipped stack check if no firmware
build exists.

Worker cleanup now resumes the network services in one place after the update
helper returns. Networking is paused once at the start of the helper; the
redundant pause immediately before download is removed. The unused update modes
and duplicate early-return cleanup have been removed. After these review changes,
all 108 Python tests, the firmware build, and layout/stack checks passed locally.
The physical results above remain
specific to commit `5bfa303`; the subsequent cleanup has not been reflashed or
retested on hardware. The signed preview assets have not been replaced.

## Superseded v0.2.8 preview

**Never deploy or promote `preview-v0.2.8-1`.** Its immutable firmware at
`c915c36` contains the updater stack defect. Fixing and validating v0.2.9 does
not make v0.2.8 safe: a clock running v0.2.8 can fail during its next OTA check
and may need USB recovery. The original v0.2.8 attempt failed on the source
v0.2.7 clock before download, so that attempt did not install v0.2.8.

The v0.2.8 Fleet candidate remains paused, and its
[release notes](https://github.com/cdemeke/SugarClock/releases/tag/preview-v0.2.8-1)
explicitly prohibit deployment or promotion. Assets remain available for
traceability; the warning and paused candidate do not prevent an administrator
from manually creating or resuming a rollout. Do not target this release.

## Limits

The interruption was a hardware reset during download, not complete power
removal: this clock has an internal battery. Full power-loss/brownout recovery,
interruption after boot-slot selection, and a deliberately crashing new image's
automatic rollback still require the separate hardware acceptance tests in
[OTA_HARDWARE_TESTS.md](OTA_HARDWARE_TESTS.md). This test does not establish
stack headroom for every possible network condition. The compiler check covers
the three named source files, not the combined call stack or prebuilt TLS/RSA
library frames; the hardware high-water measurement remains necessary.
