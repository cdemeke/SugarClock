# Configuration UI: review decisions

## Dashboard units

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

- 54 host tests pass, including 4,404 value/delta formatting comparisons against the firmware and lossless, deterministic compression selection.
- Browser checks cover mmol/L and mg/dL, signed deltas, delayed configuration responses, changing unit preferences, invalid readings, and enabled-only dashboard links.
- Brightness form tests cover unchanged-value preservation, manual selection, restoring automatic mode, and omission of hidden alarm/startup settings.
- Firmware builds successfully at 1,413,104 bytes (77.0% of the OTA slot).
- Device and Mac setup-app web assets remain byte-for-byte synchronized.
