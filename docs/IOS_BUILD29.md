# iOS 1.0.0 (29) — review fixes

Build 29 combines the short statuses/Wi-Fi recovery from build 28 with the base-branch fixes in [review follow-up](IOS_REVIEW_FOLLOWUP.md). The iPhone change ensures that discarding a single unconfirmed password replacement removes it from both persisted snapshots while keeping nonsecret uncertainty evidence. Secure-storage failures retain the edit for retry. No mutation is replayed.

## Validation

- Combined Swift suite: 182 tests executed, 3 opt-in physical-clock tests skipped, no failures.
- Complete Debug iPhone simulator build and signed Release iPhone archive passed with Xcode 27.2.
- Archive metadata verified: bundle `com.sugarclock.companion`, version `1.0.0`, build `29`.
- The base firmware/host/analytics validation is documented in the review follow-up. No firmware is bundled into this phone update, flashed to a clock, or publicly released by this iteration.
- Existing app screenshots remain representative because no layout or status wording changes in this follow-up. The onboarding concepts remain separate design artifacts.

TestFlight delivery targets SugarClock's existing **Internal** group with notes for individual discards, uncertain password updates, relaunch, storage retry and normal settings/reconnection behavior. Availability is verified separately from upload completion.
