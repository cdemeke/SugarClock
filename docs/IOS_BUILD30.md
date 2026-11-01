# iOS 1.0.0 (30) — HTTP confirmation evidence

Addresses the two Greptile P1 findings on PR #81: unfinished settings saves could appear confirmed, and queued Wi-Fi update monitoring could stop at an earlier idle/update-available state.

## Settings persistence

The firmware's `GET /api/config` now includes the Boolean `configuration_saved`, read under the same configuration guard as the live values. The Wi-Fi adapter maps only an actual Boolean `true` to the protocol's `saved: true`. Missing, false, numeric or string metadata is not durability evidence. Metadata is not offered as an editable setting.

Consequently a `persistence_failed` write followed by matching live readback retains the draft and unconfirmed receipt. A later durable read can confirm it without replaying the mutation. Legacy firmware that lacks this marker remains editable, but the app keeps those saves unconfirmed; the marker requires clock firmware containing this API addition.

## Queued firmware requests

`GET /api/ota/status` now publishes `request_pending` and `request_failed`, matching the existing BLE contract. Pending is sampled **before** the OTA snapshot so a worker finishing during the read cannot attach a terminal flag to an earlier state. Failure is exposed only after pending clears.

The adapter forwards this evidence to the coordinator. Missing/malformed evidence during a watch remains conservatively pending. Before submitting a new Wi-Fi check/install, the app requires the endpoint to support both Boolean flags; otherwise it offers guidance to use the clock's web page and submits no request it cannot monitor. Existing BLE behavior is unchanged.

## Validation and delivery

- 188 Swift tests executed, 3 opt-in hardware tests skipped, 0 failures.
- New regressions cover legacy and malformed durability markers, pending idle/update-available responses, failure outcomes, and the production coordinator retaining update ownership until completion. A failed persistence integration test verifies draft retention, later confirmation and exactly one mutation.
- A compiled host harness exercises the production HTTP handlers for true/false durability, pending/failure combinations and completion during snapshot collection.
- All 178 Python/compiled-host tests, the complete simulator build and signed iPhone archive passed. Firmware build and OTA layout/stack guards passed: 1,753,744 bytes in the unchanged 1,835,008-byte application slot.
- TestFlight build 30 targets the existing Internal group; tester availability is checked separately from upload.

The phone update contains no firmware binary and does not flash a clock. New firmware metadata becomes available only after installing compatible clock firmware. No onboarding design is implemented, and the existing app layout is unchanged.
