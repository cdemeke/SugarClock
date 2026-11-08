# Build 31 screenshots

Captured from the current Debug app on the iPhone 18 Pro iOS 27 simulator through T3's device session. These are production views with a visible sample-data banner, not a new onboarding design or physical-clock evidence. The Wi-Fi fixture passes sample HTTP configuration through `LocalClockSchema.settings(fromHTTP:)`; diagnostics uses sample status.

- [Personal Wi-Fi](wifi.png): saved network and configured-password indicator.
- [Enterprise Wi-Fi](wifi-enterprise.png): saved security type, TTLS method, identities and CA-validation preference.
- [Diagnostics](diagnostics.png): the existing configuration-persistence row with confirmed sample status. The fixture's connection footer uses its default Bluetooth label; HTTP evidence and freshness are covered by automated client/coordinator tests.

To reproduce, build Debug, install on a simulator, then use the device-bound `agent-device open com.sugarclock.companion --relaunch --launch-args=--sugarclock-screenshot=<screen>` followed by `agent-device screenshot <file>`. Screens are `wifi`, `wifi-enterprise`, and `diagnostics`. Screenshot fixtures are excluded from Release and never replace a failed real connection.
