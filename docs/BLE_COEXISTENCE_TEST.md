# USB Bluetooth / network coexistence experiment

**Failed the September 8, 2026 USB stability test. Do not promote this profile
to a normal release.** The previous reduced-TLS firmware is the recovery image.

Build with `pio run -e esp32dev_ble_coexist_test`. This development profile keeps
Bluetooth enabled when ordinary glucose, weather, probe and management requests
acquire the network lease. It requires the reduced 4,096-byte TLS send buffer;
encryption and the 16,384-byte receive capacity remain unchanged. The default
`esp32dev` profile retains its network-pause fallback.

The lease still serializes internet requests and defers their start briefly for
pairing or an active settings transfer. Firmware-update work retains its explicit
Bluetooth suspension. The existing idle/session timeouts and iOS background
behavior are also unchanged; this experiment does not promise a permanent phone
connection.

Test-only diagnostics record heap availability at network begin/end, whether a
phone is connected, and allocation failures. The allocation failure callback
only updates atomic counters; the main loop emits their summary. These records
contain no settings, credentials or glucose values.

## Physical test procedure

1. Save a private flash backup, establish the active application slot and retain
   the previously working application for recovery. Install only the test
   application and verify the write and unchanged non-application prefix before
   restarting the clock.
2. Observe cold provider login and several ordinary refreshes. Confirm readings
   arrive, Bluetooth is not torn down for those requests, and there are no
   allocation failures, crashes or repeated fetch failures.
3. In the existing iOS app, save a visible setting such as brightness or the pet
   companion. Check the app's confirmation and the physical clock. Keep the app
   open through a refresh and repeat the save.
4. Background the app for about five minutes, return, and save another visible
   change. Record whether the app reconnects, confirms the save, and shows the
   updated setting after a refresh. Correlate timestamps with the filtered USB
   connection and network log.
5. Restore the working application if simultaneous operation causes memory
   failures, crashes or loss of normal reading refreshes. Successful advertising
   and fetching alone do not establish phone-connected save reliability.

Do not change the poll interval during this comparison. Broader qualification
still needs provider reauthentication, weather, repeated phone transfers and
firmware-update tests in [BLE_ACCEPTANCE.md](BLE_ACCEPTANCE.md).

## September 8, 2026 result

The owner authorized this test on the USB TC001. The installed experiment was
1,585,744 bytes (249,264 bytes of application-slot headroom), SHA-256
`e379c1ff2d45e23a6e7ce6a52b96a344ac2137159e011eafeef2524440be68fe`.
Its application-only write was hash-verified, and the entire first 64 KiB matched
the fresh backup before boot. The existing poll interval was 60 seconds and was
not changed. Both normal and experiment firmware builds, 53 repository tests,
and the layout check passed before the physical result below.

At 20:46:12 UTC the clock booted with the test flag and the verified 4,096-byte
TLS send capacity. It completed a cold Dexcom login and received a reading with
Bluetooth enabled. The first glucose worker's lifetime minimum heap was 14,188
bytes. That initial success did not persist: allocation failure reports began
about 32 seconds after boot, followed by a panic/backtrace and an unexpected
restart at about 41 seconds. Another unexpected restart followed at about 51
seconds. A second reading was subsequently received, but allocation failures
recurred. The capture recorded nine failed allocations (including 2,308-byte and
4-byte requests), two reset-reason-4 restarts, and no phone connection events.

The test was stopped and the previous reduced-TLS application restored and
hash-verified (`f37fb3d74da54bae22aced3c6491f63c319d120a9a76e69aa0d8261c943d14da`).
The entire first 64 KiB matched immediately before and after recovery. All 79
saved configuration entries and 16 Bluetooth bond entries also matched the
pre-experiment backup. The clock rebooted at 20:50:40 UTC, logged in and received
a reading; its first glucose worker ended with a 41,804-byte lifetime minimum.
The first three recovery glucose workers completed, with two captured reading
receipt events, no captured allocation/crash faults and no unexpected restart.
The lifetime minimum remained 36,352 bytes through the third worker.

These observations fail coexistence stability even without an active phone.
They do not identify the
precise crashing code path: the privacy filter retained fault categories rather
than backtrace addresses, and some interleaved serial output may be omitted.
An isolated successful Dexcom handshake does not prove all other network work
can coexist with Bluetooth. Further work needs a symbolized crash trace and
memory measurements per network worker before another phone save test.
