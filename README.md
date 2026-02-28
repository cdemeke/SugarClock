<p align="center">
  <img src="docs/images/logo.png" alt="SugarClock" width="150" style="border-radius: 20px;">
</p>

<h1 align="center">SugarClock</h1>

<p align="center">
  Turn a $40 pixel clock into a real-time glucose display for your desk, nightstand, or kitchen counter.
</p>

<p align="center">
  <a href="https://cdemeke.github.io/SugarClock/">Website</a> &bull;
  <a href="https://amzn.to/4rrqbjz">Buy the Clock (~$40)</a> &bull;
  <a href="https://cdemeke.github.io/SugarClock/setup.html">Setup Guide</a> &bull;
  <a href="https://cdemeke.github.io/SugarClock/support.html">Help & FAQ</a>
</p>

---

## What is SugarClock?

SugarClock is free, open-source firmware that turns the [Ulanzi TC001 Smart Pixel Clock](https://amzn.to/4rrqbjz) into a dedicated CGM (continuous glucose monitor) display. It connects to **Dexcom Share** or **Nightscout** over WiFi and shows your current glucose reading in big, color-coded numbers on an LED matrix.

**Cost:** ~$40 one-time for the clock. The software is free. No subscriptions.

## Features

- **Live glucose display** — Large color-coded numbers (green = in range, orange = high/low, red = urgent)
- **Trend arrows** — See which direction your glucose is heading
- **Dexcom Share & Nightscout** — Works with Dexcom CGMs directly, or any Nightscout-compatible setup
- **Audible alerts** — Buzzer for high/low glucose with snooze button
- **Auto brightness** — Built-in light sensor adjusts to your room
- **Night mode** — Dims automatically during sleeping hours
- **Web dashboard** — Configure everything from your phone or computer browser
- **Clock, weather & more** — Also shows time, date, temperature, pomodoro timer, and push notifications

## What You Need

| Item | Notes |
|------|-------|
| [Ulanzi TC001 pixel clock](https://amzn.to/4rrqbjz) | ~$40 on Amazon |
| USB-C data cable | Usually included with the clock |
| A Mac (or any computer) | Mac has a one-click installer; Windows/Linux can use the command line |
| WiFi (2.4 GHz) | The clock connects to your home WiFi |
| Dexcom account or Nightscout URL | Your glucose data source |

## Quick Start (Mac)

### 1. Download the installer

Download **SugarClock Setup.dmg** from the [latest release on GitHub](https://github.com/cdemeke/SugarClock/releases/latest). Open the DMG and drag **SugarClock Setup** into your Applications folder.

### 2. Plug in the clock

Connect your Ulanzi TC001 to your Mac with the included USB-C cable. Use a port directly on your Mac (not a hub).

### 3. Run the setup app

Open **SugarClock Setup** and follow the on-screen steps. The app will walk you through everything:

1. **Detect your clock** over USB
2. **Pick your WiFi network** from a list
3. **Connect your glucose source** (Dexcom Share, Nightscout, or custom URL)
4. **Set your preferences** (units, alerts, brightness, timezone)
5. **Flash the firmware** — the app installs everything onto the clock automatically

When it's done, the clock restarts and your glucose reading should appear within a minute.

For detailed step-by-step instructions (with screenshots), see the **[Setup Guide](https://cdemeke.github.io/SugarClock/setup.html)**.

<details>
<summary><strong>Advanced: Build from source (all platforms)</strong></summary>

If you'd rather build and flash manually (or you're on Windows/Linux):

```bash
pip install platformio
git clone https://github.com/cdemeke/SugarClock.git
cd SugarClock
pio run && pio run --target buildfs      # build firmware + filesystem
pio run --target upload                   # flash firmware
pio run --target uploadfs                 # flash filesystem
```

Then set your WiFi credentials in `src/config_manager.cpp`, rebuild, and re-flash. Use `pio device monitor` to find the device IP, then open `http://<device-ip>/config.html` to configure your glucose source.

You may need the [CH340 USB driver](https://sparks.gogo.co.nz/ch340.html) on Windows.

</details>

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Clock not detected via USB | Use a data cable (not charge-only), connect directly (no hub), install CH340 driver |
| Upload fails | Hold middle button while plugging in USB to enter flash mode |
| `NO WIFI` on display | Check SSID/password, make sure it's a 2.4 GHz network |
| `NO DATA` on display | Check Dexcom credentials or server URL on the config page |

See the **[Help & FAQ](https://cdemeke.github.io/SugarClock/support.html)** for more.

## Backup & Restore Factory Firmware

```bash
# Backup original firmware (before flashing)
esptool.py -p /dev/cu.usbserial-* -b 921600 read_flash 0x0 0x800000 tc001_factory_backup.bin

# Restore original firmware
esptool.py -p /dev/cu.usbserial-* -b 460800 write_flash 0x0 tc001_factory_backup.bin
```

## Acknowledgments

- [AWTRIX3](https://blueforcer.github.io/awtrix3/#/) — LED matrix firmware for the TC001 that inspired this project
- [pydexcom](https://github.com/gagebenne/pydexcom) — Dexcom Share API reference
- [OpenWeatherMap](https://openweathermap.org/) — Free weather API

## License

MIT
