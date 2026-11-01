# Local-network identity and Wi-Fi scan review follow-up

The existing Wi-Fi client trusted an address after its first greeting. A clock at that address could change after DHCP reassignment or an iPhone network change. The client now retains the saved/greeted hardware identity, checks it immediately before every mutating HTTP request, and validates identity after reads before returning settings or diagnostic evidence. Cached schema reads also check identity. A mismatch discards cached configuration and reports a retryable disconnection, allowing the existing coordinator to rediscover the original clock without replaying a mutation. Optional diagnostic failures cannot hide a mismatched identity and leave the session ready.

The scan adapter also checks the firmware's error field before returning networks. An accepted scan that later fails no longer supplies cached results as a successful fresh search. The existing scan error message is shown, previous successful results stay unchanged, and an explicit retry can succeed normally.

Validation:

- The four new adapter regression tests fail against the previous implementation and pass after these fixes.
- `swift test --package-path ios`: 207 tests executed, 3 existing live-device tests skipped, zero failures.
- Regression coverage checks direct settings saves and settings.patch, Wi-Fi trial/scan, both OTA commands, identity changes during readback, repeat greetings, cached schema, automatic rediscovery at a new address without sending/replaying the blocked mutation, and scan failure/retry presentation state.
- The complete unsigned Debug iOS Simulator SDK build passes with `xcodebuild -project ios/SugarClock.xcodeproj -scheme SugarClock -sdk iphonesimulator -configuration Debug CODE_SIGNING_ALLOWED=NO build` (using a temporary derived-data directory).
- `git diff --check` passes.

These changes affect transport validation and existing error paths, with no layout changes. New screenshots are not applicable; the PR's existing UI screenshots remain representative. No real clock was changed or flashed, and this follow-up does not publish firmware or an app release.
