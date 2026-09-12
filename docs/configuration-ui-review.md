# Configuration UI: review decisions

## Dashboard units

The visible Live view now uses `/api/display/frame`, a 768-byte RGB snapshot of the last rendered 32 × 8 LED frame. Previous/next, physical button presses and auto-cycle are reflected automatically. Logical row order is restored from the serpentine wiring; frame publication/copy is protected across render and HTTP tasks. Browser rendering uses full-color pixels for readability, not the physical LED brightness/power calibration. A separate, visible **Latest blood sugar** section always shows the reading, signed delta (including zero), and data age independently of the selected LED screen. It identifies stale, missing, and unavailable readings explicitly and has a semantic, accessible section name.

The browser polls at most four times per second with one request in flight, stops while hidden, times out failed requests and shows an explicit disconnected state before retrying. The local staging server proxies this read-only frame endpoint too.

Frame capture runs at most four times per second and stops five seconds after the last request; devices with no viewers do not build snapshots. A new viewer after idle gets a body-free 204 until the render task publishes a fresh frame. Two static buffers avoid per-capture zeroing and full-buffer publication under the lock: the renderer fills the inactive buffer and swaps its index under a short lock. Reader copies remain protected. A pixel comparison preserves the sequence when the image is unchanged. ETags combine that sequence with a random boot epoch, and the browser sends `If-None-Match`: unchanged frames return a body-free 304 without copying pixels or repainting the canvas. Changed-frame responses own a fixed-size frame member for their asynchronous lifetime instead of growing a 768-byte String. Normal response objects and HTTP headers still allocate; this is not an allocation-free endpoint.

All `.hint` helper text, including text outside form groups, now uses the same compact 12px, muted style.

Device APIs continue returning glucose and delta in mg/dL. `/api/status` now also includes `use_mmol`, so the dashboard formats a reading and its unit together without racing a separate configuration request. Older firmware falls back to the saved configuration. Both the reading and signed delta use the same /18 conversion and one-decimal rounding as `include/glucose_format.h`. Invalid readings do not leave a stale delta visible.

## Intentional simplifications

- **Buzzer Alerts stays hidden.** The product owner requested this because the device's beep is too quiet. This change does not delete the firmware capability, switch existing alarms off, or change stored alarm settings. The configuration form omits those fields so unrelated saves preserve them. Alert settings remain accessible through the existing configuration API, not the redesigned web form.
- **Default View stays hidden.** This is a deliberate behavior change requested by the product owner. The web form no longer changes the saved startup view (`default_mode`). Existing settings remain intact; screen navigation and auto-cycle remain available.
- **Dashboard cards are enabled-feature links.** Timer, stopwatch, weather, system-monitor and countdown values are intentionally no longer duplicated on the dashboard. Only enabled features appear, linking to their configuration sections. This does not remove their firmware behavior or API values.

## Brightness

The main control remains a 1–100% slider. Moving it selects manual brightness. A collapsed **Advanced brightness** section now allows automatic light-sensor adjustment to be turned back on, avoiding the previous one-way transition. Loading and saving without touching brightness preserves the exact device value; neither automatic mode nor hidden alarm/startup settings are reset by unrelated form edits.

## Icon size and caching

The four PNGs are supplied artwork chosen by the product owner, also displayed next to page headings. They are retained rather than replaced by visually different SVG approximations. Their original total is 81,509 bytes. The asset generator now selects gzip only when it is smaller, reducing their stored payload from 81,243 to 81,159 bytes (84 bytes saved, not a material firmware-size reduction). Three icons are served without Content-Encoding; the logo and compressible text assets remain gzipped. Further substantial savings would require a separate artwork/format decision.

The `?v=toast-icons-1` stylesheet suffix is removed. The existing no-cache policy and content-derived ETags remain.

## Verification

- 58 host tests pass, including 4,404 value/delta formatting comparisons against the firmware, visible-summary and reading-age checks, lossless compression selection, completed-frame checks, polling/visibility/recovery checks and global hint styling. Demand/expiry, rate limiting, unchanged frames, fresh capture after idle, and timer rollover are covered. The zigzag host stub exercises real drawing; a deliberate progressive-mapping mutation must fail the same render-path test. Conditional 304 responses do not read a body or repaint; 204 warm-up responses are handled explicitly.
- Browser checks cover mmol/L and mg/dL, signed deltas, delayed configuration responses, changing unit preferences, invalid readings, and enabled-only dashboard links.
- Brightness form tests cover unchanged-value preservation, manual selection, restoring automatic mode, and omission of hidden alarm/startup settings.
- Browser follow-up checks cover a visible, named glucose section while another display is selected, zero delta, stale/missing/unavailable readings, conditional frames, and desktop/mobile layout without overflow.
- Firmware builds successfully at 1,417,088 bytes (77.2% of the OTA slot).
- Device and Mac setup-app web assets remain byte-for-byte synchronized.
