# SugarClock app showreel (15 s)

`sugarclock-showreel.mp4` is a 1920×1080, 30 fps, 15-second promo with a
synthesised, playful 120 BPM soundtrack. The pixel content on every clock
comes from the demo studio's production-parity renderers
(`../demo/pixel-renderer.js`), and the readings and settings are realistic
mock data.

| Time | Scene |
| --- | --- |
| 0–2 s | Pixels assemble into a live reading, then the SugarClock logo |
| 2–4.5 s | Live glucose on your desk · Dexcom, FreeStyle Libre, Nightscout |
| 4.5–7 s | Colour-coded ranges and trend arrows with a 3-hour CGM chart (in range → high → urgent low + snooze) |
| 7–9.5 s | All seven Pixel Companions |
| 9.5–11.5 s | Clock, weather, focus timer and countdown screens |
| 11.5–13.5 s | Setup from your phone, then night mode dims the clock |
| 13.5–15 s | Logo, "Free, open-source · No subscriptions", sugarclock.com |

## Rebuild

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory docs &   # from repo root
cd docs/showreel
node render.mjs frames 30            # needs the `playwright` package + Chromium
python3 music.py music.wav            # needs numpy
ffmpeg -framerate 30 -i frames/f_%04d.png -i music.wav \
  -c:v libx264 -pix_fmt yuv420p -crf 18 -preset slow \
  -c:a aac -b:a 192k -shortest -movflags +faststart sugarclock-showreel.mp4
```

Open http://127.0.0.1:8765/showreel/showreel.html to preview the animation
live in a browser (silent). `renderAt(seconds)` draws any single frame.
