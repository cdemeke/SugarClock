# iOS Wi-Fi connection — build 25 (October 27, 2026)

## Why the Office clock stopped connecting

The app had only one link: Bluetooth, which exists only in this branch's
companion firmware (0.3.x). The Office/USB dev clock (`192.168.1.81`, identity
`DC20F8319D48`) ran companion firmware 0.3.6 from October 2. On October 24 the
0.2.18 admin-tooltip candidate was deployed to that installation through Fleet
Manager. Mainline 0.2.x has no Bluetooth code, so the clock stopped advertising
and the app could never find it, however long it retried. All three clocks on the
home network now run mainline firmware (0.2.15 or 0.2.18).

Until PR #29 merges, every mainline release or candidate installed on a
companion-firmware clock removes Bluetooth. Even on companion firmware, the
TC001's lack of PSRAM means Bluetooth shuts down for every HTTPS request (glucose,
weather, network checks). The clock also drops idle phones after 60 seconds and
caps sessions at 10 minutes, so a Bluetooth-only app reconnects every few minutes.

## What changed

The app now prefers the clock's existing local web API (the same API the admin
page uses) and keeps Bluetooth as a fallback.

- **Any firmware.** `LocalNetworkClockClient` maps the app's protocol
  operations onto `/api/config`, `/api/status`, `/api/wifi/*`, `/api/ota/*` and
  `/api/debug`. Settings use the Bluetooth shape: secrets only as
  `*_configured` flags, and colors as integers. Mainline firmware has no schema
  endpoint, so the app carries the firmware's field table and offers only the
  keys the clock returns.
- **Same clock, either link.** Identity is the station MAC from `/api/debug`
  with its bytes reversed. That equals the firmware's `%012llX` eFuse identity
  sent in the Bluetooth hello, so a clock saved over Bluetooth is recognized on
  Wi-Fi with no re-adding.
- **Finding it.** Saved clocks remember their last Wi-Fi address. If that
  address fails, the app searches the phone's Wi-Fi subnet (64 probes in
  parallel, nearest addresses first, at most once per 30 s in the background).
  Only an address answering with the expected identity is accepted, which covers
  DHCP changes. Add clock lists clocks found on Wi-Fi and accepts a typed IP
  address.
- **No stalls.** The saved address is tried first; it either answers in under a
  second or fails within 2.5 s. Bluetooth then starts at once, while the Wi-Fi
  search and address checks run alongside it. Whichever link answers first wins,
  so a clock that has stopped advertising no longer holds a 60-second Bluetooth
  attempt. If Bluetooth wins and Wi-Fi turns up afterwards, an idle session moves
  to Wi-Fi (at most once a minute, never during a save, command or update).
  Bluetooth disconnect events cannot end a Wi-Fi session.
- **Unchanged guarantees.** Saves are still confirmed only by readback and never
  replayed automatically. Offline drafts, OTA ownership and the Bluetooth
  security model are untouched.

Limits over Wi-Fi with mainline firmware:
- Saved secrets cannot be cleared, because the web API treats an empty value as
  "unchanged". The app says so instead of reporting a false success.
- Changing the clock's Wi-Fi needs the network password, because the web trial
  cannot reuse the stored one.
- The blood-sugar switch and automatic-update settings appear only on firmware
  whose `/api/config` returns them.

The web API is unauthenticated on the local network, as it already was for the
admin page. The app adds no new exposure. Secrets typed into the app travel over
local HTTP, like the admin page, rather than over the encrypted Bluetooth bond.

`Info.plist` adds `NSLocalNetworkUsageDescription` (iOS asks once) and
`NSAllowsLocalNetworking`. The encryption declaration is unchanged.

## Verification

- 172 Swift tests pass (150 existing, 22 new; the 2 live tests skip unless
  enabled), stable over five consecutive
  runs. The new tests use a fake mainline web server and cover:
  - identity derivation and protocol mapping
  - secret and color translation, and the clear-secret refusal
  - Wi-Fi trial password rules and error mapping
  - subnet bounds and discovery among non-clock devices
  - Wi-Fi preference with Bluetooth fallback, DHCP moves and identity
    mismatches
  - Bluetooth events during a Wi-Fi session
  - adding over Wi-Fi, outage recovery, and a pending Bluetooth attempt yielding
    to Wi-Fi
  - Bluetooth-to-Wi-Fi upgrade and Wi-Fi with Bluetooth off
- Two opt-in tests (`SUGARCLOCK_LIVE_HOST=192.168.1.81 swift test --package-path
  ios --filter LiveClockTests`) passed against the real Office clock on 0.2.18:
  - Discovery found all three home clocks in 6.1 s cold and 1.7 s warm (22 s
    before tightening probe timeouts).
  - Connecting to a clock saved with only its identity took 6.7 s, including
    the search. A warm reconnect took 0.33 s. Hello, settings, schema and status
    together took about 0.5 s.
  - Each run rewrote the clock's current brightness unchanged and confirmed it
    by readback; no clock setting changed.
- The Debug simulator build and the signed Release archive passed. The archive's
  Info.plist was checked for build 25, the local network keys and Boolean
  `ITSAppUsesNonExemptEncryption = false`.
- **1.0.0 (25)** was uploaded to App Store Connect on October 27, 2026.

This Mac's CoreSimulator is older than Xcode 27.1 requires, so no simulator
screenshots were taken. The Add clock "On your Wi-Fi" section has not been
visually checked. On the iPhone, confirm:
- the Local Network prompt appears and is accepted
- the Office clock connects over Wi-Fi, and a brightness change shows on the clock
- leaving and rejoining Wi-Fi recovers

## Build 26: connection status badge

The clock screen shows a status badge beside the clock's name:

| Badge | Meaning |
|---|---|
| Green check | Connected |
| Pulsing orange dots | Connecting |
| Red alert | Couldn't connect |
| Gray ring | Not connected |

Tapping the badge opens a small popover with:
- the link in use: Wi-Fi with the clock's address, or Bluetooth
- the current step or failure reason
- the last sync time
- Retry, when the app isn't connected

`ClockModel.connectionStatus` derives the state, and session tests cover each
level.

Writing those tests uncovered a gap: a clock with no saved address that joined
Wi-Fi while a Bluetooth attempt was pending waited for Bluetooth to time out (up
to 60 s). The Wi-Fi side of the race now repeats the rate-limited search while
the address is unknown.

177 tests are in the suite: 175 pass in each of five runs, and the 2 opt-in live
tests skip by default. The live tests passed again against the Office clock
(6.4 s first connect, 0.34 s warm reconnect).

## Build 27: status for every clock in My Clocks

Each row in My Clocks shows the badge icon left of its status line:

| Icon | Line | Meaning |
|---|---|---|
| Green check | Connected over Wi-Fi / Bluetooth | The open clock's session is ready |
| Green check | Available on Wi-Fi | Another clock answers at its address with its identity |
| Orange dots | Checking Wi-Fi… / Connecting | First check, or the open clock is reconnecting |
| Gray ring | Not found on Wi-Fi | No answer; opening it still tries Bluetooth |

The app keeps one editing session, for the open clock. Wi-Fi requests are
stateless, so "Available" means the clock opens in well under a second.
`ClockModel.refreshAvailability()` runs when the list appears and every 15 s
while it is on screen. It never touches the open session.
- Each clock is checked at its saved address with short timeouts. Only an
  answer with the saved identity counts, so a different device at the address
  doesn't count.
- A clock missing from its address (a new address after a DHCP change, or no
  address yet) triggers a subnet search. That search runs at most once every
  two minutes, and never while a session search is running.

A session test covers four clocks at once:
- the open clock
- one at its address
- one moved by DHCP
- one absent

It then checks that an outage shows on the next pass without a repeat search.
The suite has 179 tests: 176 pass (five runs in a row before the live test was added), and the 3 opt-in live tests
skip by default. A new live test opened the Office clock and saved the other two
home clocks by identity only. All three showed green: the first check took 6.0 s
(it learns addresses), and later checks took 0.22 s.
