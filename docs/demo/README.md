# Virtual SugarClock demo studio

A dependency-free video demo. All readings, weather, and addresses are mocked; no account, device, API key, or network data is needed. The display uses a real 32 × 8 pixel grid and reuses the production companion preview and ports the firmware weather renderer.

From the repository root:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory docs
```

Open http://127.0.0.1:8765/demo/ in a current browser. Serve over HTTP; JavaScript modules do not load reliably from `file://`. The page can also be served with the existing static docs site at `/demo/` after merge/deployment.

## Recording walkthrough

1. Start on glucose; try the three presets and switch Dexcom to Libre for a brief identifier.
2. Select Pixel companion. Try each of the seven pets, greeting, and nap. High/Low replaces the pet with the glucose number; In Range restores it.
3. Select Weather and try all ten firmware visuals: clear, partly cloudy, cloudy, drizzle, rain, sleet, snow, thunderstorm, tornado, and the static cloud fallback, plus °F/°C conversion.
4. Choose included displays under Display rotation. Extra services includes all three timers. Enable auto-cycle at three or five seconds, or use Next.
5. Select Pomodoro, choose the ten-second demo, and start it. It switches to a five-second break and repeats. Stopwatch and event countdown use real elapsed time too; event has a 15-second shortcut.
6. Choose “Ulanzi TC001 enclosure” under Display appearance, then double-tap the middle physical button to show the editable IP, then wait six seconds for return. A single completed button gesture also dismisses it.
7. Click **Record pixels only** for a clean matrix with no enclosure, buttons, controls, or framing. Press F or Escape, or double-tap the matrix, to return. Display appearance defaults to Pixel block only and remembers your choice locally. The optional TC001 enclosure follows the published dimensions and front-on hardware photos. F / Filming mode also works with either appearance.

## Side-by-side comparison

Select Weather or Pixel companion, then enable **Compare all options · two columns** in the right panel. All ten weather conditions or all seven pets animate together, each in a TC001 enclosure with a label. Temperature/unit, glucose presets, nap, and greetings apply across the set; the selected single-display option is preserved when you turn comparison off. Other modes use the single display. Filming mode also supports the gallery; Record pixels only returns to a single borderless matrix. The studio background is white, and In Range / High / Low presets live in the right panel.

## Physical buttons

| Button | Short press | Hold one second |
| --- | --- | --- |
| Left | Next enabled display | Return to glucose |
| Middle | Cycle brightness | Simulate alert snooze |
| Right | Pet greeting; timer start/pause; otherwise previous display | Reset selected timer; otherwise return to glucose |

Double-tap middle within 350 ms for IP. Buttons also accept keyboard Enter/Space; double activation of middle invokes IP. Long holds use pointer/touch. No actual buzzer or alert audio is played.

## Fidelity and intentional demo behavior

Glucose presets, source selection, weather, and IP are local simulation controls. High/Low companion takeover follows the video brief; firmware only forces numeric takeover for urgent glucose. The controller makes this distinction explicit. Reading age is controller/accessibility metadata, not an extra pixel row. Status is OK beside an in-range pet, including naps and greetings, matching the production preview; alerts replace the whole pet display. Timer colors and brief WORK/BREAK labels make phase transitions clear for filming. The nap checkbox demonstrates a sleepy pose without waiting for the device's inactivity schedule. Clock uses local computer time and supports 12/24-hour presentation.

## Reuse and testing

Import `virtual-sugar-clock.js`, put `<virtual-sugar-clock>` in your HTML, then assign `.state` using a `Simulation.snapshot()` result on animation frames. The element emits a bubbling `clock-button` event with `{name, kind}`. The host handles it using `simulation.button(name, kind)` and owns the animation loop. Renderer and simulator are independent of the studio controls.

```sh
node --test tests/test_video_*.mjs
```

Tests cover elapsed-time accounting, pause/resume/reset, suspended-tab catch-up, rotation exclusions, IP restoration, button context, source intro, event completion, all seven pets and fallback, precipitation, scrolling, and pixel bounds. No firmware changes or build are needed.

## Visual references

- [PR #45](https://github.com/cdemeke/SugarClock/pull/45) and `docs/images/weather-side-animation-design.png`: 8×8 weather icon, stationary compact 4×7 digits with degree/unit, static sun, looping clouds, dense rain, six-armed snowflakes without a cloud, and amber lightning that brightens periodically. `weather-renderer.js` ports `src/weather_render.cpp`; tests compare frames directly against compiled firmware rendering.
- [PR #28](https://github.com/cdemeke/SugarClock/pull/28) and `docs/images/pixel-companions.png`: `companions.js` is an unchanged copy of `data/www/companions.js`. Parity tests check the copy and all pets' animation, nap, and greeting frames against the production preview. Keep the copy synchronized when production art changes.
- Clock hour formatting and glucose default colors follow `src/display.cpp` and `src/config_manager.cpp`. Physical LED diffusion and user-configured hardware brightness/colors can differ from a browser preview.

## TC001 physical appearance

The optional enclosure uses Ulanzi’s published **200.58 × 70.25 mm** front dimensions (2.855:1). The LED matrix remains **32×8**, with equal horizontal/vertical pitch and 90% square diffuser fill in both framed and pixels-only modes. It uses a thin pale case rim, rounded black front glass, near-flush top controls and light sensor, without invented feet or front branding.

The active matrix occupies about 86.2% of the body width, centered vertically; this inset, corner radius, and diffuser fill are estimates from the linked hardware photograph, not factory mechanical measurements. Display colors preserve the source hue and saturation, with a full-intensity center and subtle edge diffusion. The demo starts at full brightness for recording; the middle button still cycles brightness levels. No animation shifts pixels between physical grid positions. Browser/monitor brightness and camera exposure cannot be calibrated from a photograph, so this is a close visual reconstruction rather than an optically exact hardware capture.

References: [Ulanzi dimensions and hardware review](https://www.ulanzi.com/de-eu/blogs/news/ulanzi-desktop-pixel-clock-tc001-review), [illuminated matrix/front photograph](https://blakadder.com/assets/images/ulanzi-tc001/pixelrows.jpg), [top buttons and light sensor](https://blakadder.com/assets/images/ulanzi-tc001/buttons.jpg). Geometry is centralized in `device-geometry.js` and tested across six display widths. Reference photographs are not bundled or used as runtime assets.
