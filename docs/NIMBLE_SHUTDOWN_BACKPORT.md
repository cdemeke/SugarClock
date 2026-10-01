# NimBLE host shutdown backport

The companion firmware remains on NimBLE-Arduino **2.5.0**, Arduino ESP32
2.0.17 / IDF 4.4.7, and the existing dual-slot partition layout. A narrowly scoped
build-time backport applies upstream commit
[`e0c8f5a558893197ae60c3606ded01a2dbb88863`](https://github.com/h2zero/NimBLE-Arduino/commit/e0c8f5a558893197ae60c3606ded01a2dbb88863).
The 2.5.1 release does not include this fix; upgrading to that release alone
does not address the fault.

On the USB test clock, Bluetooth shutdown for an HTTPS request repeatedly
produced an `InstrFetchProhibited` exception in the NimBLE host task, followed
by a restart. The upstream defect explains how this can happen: a host timer
event can already be queued when shutdown stops the timer. Prematurely
deinitializing the timer clears that queued event's callback. The host then
attempts to execute an invalid callback. This backport removes only the early
`ble_npl_callout_deinit(&ble_hs_timer)` call from `ble_hs_timer_reset()`;
`ble_hs_deinit()` retains final cleanup after the host has stopped.

`scripts/patch_nimble.py` runs after PlatformIO resolves the environment's
dependencies and before compilation. It modifies only the environment-local
library, applies atomically, and accepts exactly the pristine 2.5.0 source or
the already patched result. Unknown versions or bytes stop the build. There
are no downloads or shared SDK modifications in this hook. Deleting `.pio`
and reinstalling dependencies automatically reapplies the fix.

| `ble_hs.c` content | SHA-256 |
| --- | --- |
| Upstream 2.5.0 | `c5677503b264738b692d0e688672011206a308d20c5ca1aa5e32a649443bd48a` |
| With the upstream one-line fix | `8232c5a8721f8a75dc08dd93a7b7a1690c16a57f0c8f4660029c45e5a27be324` |

Run `pio pkg install -e esp32dev`, then
`python3 -m unittest discover -s tests -p 'test_nimble_shutdown.py' -v`.
The regression compiles the actual pristine and patched upstream timer-reset
and final-deinitialization functions against a queued-event harness. The
pristine function loses its callback; the patched function preserves it through
1,000 shutdown/reinitialization cycles and releases the timer during final
cleanup. Other tests cover version/source mismatches, repeated builds, a fresh
environment, and interrupted writes. This host test does not establish real
radio stability; repeated physical pairing, HTTPS, OTA and reconnect checks
remain necessary.

When updating NimBLE, audit the chosen release for this upstream fix, remove
the hook once it is included, and rerun the physical acceptance checks. Do not
update the allowed source checksums without reviewing the changed code.
