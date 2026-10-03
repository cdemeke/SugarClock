# Grouped management and timestamp-based Dexcom polling

The normal firmware retains Bluetooth shutdown during TLS. This change reduces
how often ordinary requests need that shutdown; it does not enable the failed
Bluetooth/TLS coexistence experiment or increase TLS memory allocations.

## Dexcom

The first fetch is immediate when the network is available. After a successful
reading with a usable UTC timestamp, schedule the next request at that timestamp
plus five minutes and a 15-second publication allowance. Use a monotonic deadline
after calculating the delay so later NTP adjustments cannot cause a polling
storm. A scheduled request is never less than 15 seconds after its predecessor
completes. Explicit refresh requests bypass the schedule and are retained while
offline or waiting for the network lease.

If the expected sample has not arrived, retry after 30 seconds, then 60 seconds,
then every 120 seconds until a new timestamp restores alignment. Failed requests
back off by 60, 120 and then 300 seconds. These are distinct cases: returning
an unchanged reading is not itself a transport failure.

Missing, malformed, future, or more-than-one-hour-old timestamps, and an unset
wall clock, use the saved poll interval clamped to 15–300 seconds. The existing
poll interval field therefore remains the Dexcom fallback, not its normal
schedule. Custom HTTP sources retain their configured fixed interval; demo mode
is unchanged. Changing source settings resets alignment and fetches promptly.
The parser clears invalid dates instead of retaining a previous reading's date.

The 15-second allowance is an initial policy, not a Dexcom delivery guarantee.
Physical tests must include late uploads and reauthentication. No payloads,
credentials or glucose values are included in the new scheduling diagnostics.

## Management

Successful check-ins clamp the server's requested interval to 300–600 seconds,
with the existing +/-15-second jitter. During the final 1.5 seconds of an existing
radio pause, a check-in may start up to 60 seconds before its target. It reacquires
the exclusive network lease before Bluetooth resumes, avoiding a second radio
shutdown. This can also reuse another ordinary request's pause.

If a Dexcom poll is expected within five minutes, management waits for its pause,
up to five minutes beyond the management target. Otherwise, it may start
separately 60 seconds after its target. Unavailable glucose data or demo mode
cannot starve it. Existing
pairing/transfer protection still applies when acquiring the lease. Failures keep
the existing one-minute, five-minute and one-hour circuit backoff, and can wait
up to the same bounded grouping deadline; they are never retried early. Task-allocation
failures use this same backoff rather than repeatedly retrying in a batch window.

Grouping trades fewer interruptions for a potentially longer individual pause.
Remote commands are normally discovered roughly every five minutes rather than
two; a server-requested longer interval or endpoint failure can extend that.
TLS operations still run one at a time, with Bluetooth disabled and OTA traffic
retaining its existing exclusion rules. Neither iOS nor the BLE protocol changes.

The current fleet dashboard marks a device delayed after five minutes without a
check-in. It may briefly use that label with this slower cadence; the server's
connectivity presentation has not been changed or deployed in this firmware task.

## Verification

Host tests exercise the production HTTP and fleet loop functions with hardware
boundaries stubbed, plus the timestamp and batching policy. Cases include delayed
and malformed samples, retry reset, wall-clock availability, millis rollover,
manual refresh retention, source changes, custom HTTP timing, worker allocation
failure, OTA exclusion, single-request ownership and no early circuit retry.
The production BLE lease tests check both firmware profiles and the short batch
window. Run `python -m unittest discover -s tests -v` and `pio run -e esp32dev`.

On USB, capture the previous application for recovery and preserve settings and
bonds during application-only installation. Watch a cold Dexcom login, at least
two timestamp-aligned fetches, a management request grouped before Bluetooth
resumes, and absence of allocation/crash faults. A physical phone save test is
still needed to establish the user-visible improvement.

## USB test on September 8, 2026

The normal firmware image is 1,585,920 bytes, SHA-256
`bdb3f213c7e3d3b827c53f2741b38854d6bcb8ba922b7d85fbb0a464d5102cbf`,
leaving 249,088 bytes in the application slot. Static RAM is 93,536 bytes.
Both normal and coexistence-test profiles compiled; only the normal profile was
installed. All 55 repository tests and the layout check passed.

The USB installation was application-only and hash-verified. The complete first
64 KiB, including settings, Bluetooth bonds, bootloader, partitions and OTA
metadata, matched the fresh private backup before restarting. The filesystem and
other application slot were outside the write range. No iOS build was changed.

The clock booted at 21:30:11 UTC, logged into Dexcom, received a reading and
scheduled its next poll in 246 seconds using that reading's timestamp. The saved
60-second fallback interval was not changed. Initial phone connections included
one remote termination and three controller-reported supervision timeouts, all
with `network=0`; these were not intentional TLS pauses. A subsequent authenticated
connection remained active while the owner tested frequent setting changes.
The owner reported an initial drop followed by substantially better behavior
with frequent changes. This is encouraging user feedback, not a claim that all
disconnects or background/return cases are resolved.

The first scheduled fetch began at 21:34:28.534 UTC, matching the 246-second
deadline after the previous fetch completed. It received a reading and scheduled
the next poll in 298 seconds. Management began with `grouped=1` at 21:34:30.137,
completed successfully at 21:34:33.633 and set a 303-second target. Bluetooth
resumed at 21:34:35.336 and the phone connected/authenticated afterward. Thus the
first grouped glucose/management operation used one pause of approximately
6.8 seconds, including the resume grace, rather than two separate pauses.
Lifetime minimum heap remained 31,916 bytes through that operation; no captured
allocation/crash fault or unexpected restart occurred during this initial check.
Further phone terminations/timeouts outside network work were still observed.
