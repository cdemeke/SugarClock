# Nighttime brightness: issue #54

## Findings

The former 1–100% slider saved `round(percent * 255 / 100)`. Its minimum was device level **3**, and it skipped levels **1** and **2** even though the configuration API accepts them. The new slider uses integer device levels directly and displays the corresponding percentage to one decimal place. Dimmer/Brighter buttons and keyboard arrows adjust one device level; no percentage conversion is used when saving.

| Device level | Displayed percentage | Former slider | New slider |
| --- | --- | --- | --- |
| 1 | 0.4% | Unreachable | Selectable |
| 2 | 0.8% | Unreachable | Selectable |
| 3 | 1.2% | Shown as 1% | Selectable |
| 40 | 15.7% | Shown as 16% | Selectable, saved exactly |
| 255 | 100% | Selectable | Selectable |

These percentages describe the control value, not measured light output or perceived brightness. Automatic brightness still uses levels 5–200; moving the manual control disables it. Night Mode takes precedence during its scheduled hours and already accepts levels 1–255.

## Preventing low-level truncation

Source inspection found that exposing level 1 alone would be insufficient. The pinned Framebuffer GFX 1.1.0 library converts RGB565 through `gamma5`/`gamma6` in `expandColor`. For example, the default in-range green `#34A853` becomes `(9,110,20)` before FastLED brightness scaling. With dithering disabled, the pinned FastLED 3.10.3 ESP32 I2S path uses fixed `scale8`: `floor(channel * (brightness + 1) / 256)`. At brightness 1, all three channels of that green become zero.

`display_show()` now copies the drawing buffer into a separate static output buffer (768 additional bytes). If a nonblack pixel would become entirely black, only its strongest channel(s) are raised just enough to produce one output unit. Already-visible pixels and black pixels are unchanged. Corrections never accumulate in the source image or affect the browser frame. No temporal dithering, alternating frames, or refresh changes are introduced; global brightness and the power limiter remain configured as before.

The existing stale-warning dimming path also divided effective brightness by three, which would turn levels 1 and 2 into 0. It now keeps nonzero manual/night brightness at a minimum of 1 while preserving an explicit 0. Engine tests cover both manual and scheduled night settings.

At steady display brightness, before any power limiting, representative output bytes are:

| Color after gamma | Level 1, previously | Level 1, now | Level 2, now | Level 3, now |
| --- | --- | --- | --- | --- |
| White `(255,255,255)` | `(1,1,1)` | `(1,1,1)` | `(2,2,2)` | `(3,3,3)` |
| Default green `(9,110,20)` | `(0,0,0)` | `(0,1,0)` | `(0,1,0)` | `(0,1,0)` |
| Stale gray `(54,65,54)` | `(0,0,0)` | `(0,1,0)` | `(0,1,0)` | `(0,1,0)` |
| Black `(0,0,0)` | `(0,0,0)` | `(0,0,0)` | `(0,0,0)` | `(0,0,0)` |

This is a source-level calculation, not a measurement from a clock. It does not establish that the earlier numeric-control report is a reproduced bug in current hardware/firmware.

## Hardware limits and off behavior

[Worldsemi specifies 8-bit, 256-level color control for the WS2812B family](https://www.world-semi.com/web/index.php?classid=302&id=299&lanstr=en&topclassid=16). The smallest steady nonzero channel value is 1. Some low settings therefore produce the same output, and color fidelity is limited near minimum; for example, the gamma-expanded stale gray becomes green at the smallest output. The safeguard prioritizes retaining lit pixels over exact hue. The default green is already at its minimum steady output at level 3, so levels 1 and 2 cannot make it dimmer without changing how the display is driven. White and other brighter colors can use the newly accessible steps.

Further dimming below a channel value of 1 would require time averaging or hardware changes. Time averaging could introduce flicker or sparkle, so it is not enabled by this change. Whether the remaining minimum is comfortable in a dark bedroom requires testing on the actual panel.

The configuration controls allow only levels 1–255 and label the minimum as **not off**. Existing renderer brightness 0 and fully black transition frames remain off; the safeguard is bypassed when effective output brightness is zero. Black source pixels stay off at every setting. No new off control is added.

## Validation

- Form regression tests exercise all 255 saved values with automatic mode both on and off, exact load/save preservation, the 3 → 2 → 1 sequence, distinct labels, endpoint bounds, and restoring automatic mode.
- Renderer tests execute `display_show()` across 1–255 using representative post-gamma pixels and model the pinned FastLED scaling. They check nonzero output, unchanged already-visible pixels, neutral equal-channel pixels, deterministic repeated frames, unmodified drawing/browser frames, black pixels, brightness 0, and fully black transitions. They do not measure panel light output or flicker.
- Browser checks cover actual form submission against a mock configuration API, reload persistence, keyboard Home/End and arrow controls, automatic-mode restoration, mobile overflow, and JavaScript errors. Screenshots show the actual configuration UI with synthetic settings, not a physical display.
- Firmware and installer filesystem build; partition/OTA layout checks pass. Device and onboarding web assets are synchronized and embedded assets regenerated.

## Pending physical acceptance

Issue #54 remains open until these checks are recorded. No physical dark-room test has been performed for this change.

1. On a test TC001 in a dark bedroom, disable Automatic brightness and Night Mode, record its firmware and panel identity, then compare saved levels 3, 2, and 1 after allowing eyes to adapt. Record comfort and reading distance.
2. Check default green, urgent red, yellow, stale gray, white time, and a dim custom color. Check complete digits, trend arrows and delta text, hue changes, and whether adjacent levels actually look different.
3. Watch static readings, scrolling, transitions and animated content for flicker or colored sparkle. Verify black transition frames still work and repeated frames remain steady. Camera exposure alone is not evidence of perceived brightness or flicker.
4. Repeat with Night Mode brightness 1 and 2, verify scheduled precedence, then restore automatic brightness. Verify reboot persistence and restore the tester's original settings afterward.
5. If minimum is still too bright, record that result as a hardware-limit finding rather than claiming software acceptance or enabling untested dithering.
