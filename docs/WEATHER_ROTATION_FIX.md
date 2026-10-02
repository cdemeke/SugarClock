# Weather rotation recovery (firmware 0.3.6)

The USB test clock had weather enabled with no API key, so every rotation
included the weather renderer's `WX...` waiting screen. Disabling weather on
October 1 persisted successfully and navigation excluded it. On October 2,
weather was enabled again without a key; the available diagnostics do not record
which client/action changed it. Automatic rotation remained enabled at 10 seconds.

Rotation now requires both the weather toggle and a received weather reading.
This applies to manual navigation and automatic cycling because they share the
same screen list. The existing five-second rebuild admits weather after its
first successful fetch, and removes a selected weather screen if data becomes
unavailable. A default-weather selection also falls back until data is available.
Weather fetching remains enabled according to user settings; the fix does not
delete credentials or switch off automatic rotation. Explicit diagnostic/server
forced screens retain their existing behavior. Existing cached weather freshness
behavior is unchanged.

Host regression coverage exercises enabled-but-empty weather, successful first
fetch, loss of weather data while selected, and disabled weather with cached
data. All 74 repository host tests passed. No iOS code or protocol changes are
required; TestFlight build 24 remains compatible.
