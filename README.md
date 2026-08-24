<p align="center">
  <img src="docs/images/logo.png" alt="SugarClock" width="150" style="border-radius: 20px;">
</p>

<h1 align="center">SugarClock</h1>

<p align="center">
  Turn a $40 pixel clock into a real-time glucose display for your desk, nightstand, or kitchen counter.
</p>

<p align="center">
  <a href="https://sugarclock.com/">Website</a> &bull;
  <a href="#quick-start">Quick Start</a> &bull;
  <a href="https://sugarclock.com/setup.html">Setup Guide</a> &bull;
  <a href="https://sugarclock.com/support.html">Help & FAQ</a>
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
- **Secure WiFi updates** — Signed, power-loss-safe firmware updates with automatic rollback
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

For detailed step-by-step instructions (with screenshots), see the **[Setup Guide](https://sugarclock.com/setup.html)**.

<details>
<summary><strong>Advanced: Build from source (all platforms)</strong></summary>

If you'd rather build and flash manually (or you're on Windows/Linux):

```bash
pip install -r requirements-build.txt
git clone https://github.com/cdemeke/SugarClock.git
cd SugarClock
pio run && pio run --target buildfs      # build firmware + filesystem
pio run --target upload                   # flash firmware
pio run --target uploadfs                 # flash filesystem
```

Then set your WiFi credentials in `src/config_manager.cpp`, rebuild, and re-flash. Use `pio device monitor` to find the device IP, then open `http://<device-ip>/config.html` to configure your glucose source.

You may need the [CH340 USB driver](https://sparks.gogo.co.nz/ch340.html) on Windows.

</details>

## Firmware Updates

Version 0.2.0 is the one-time OTA bootstrap release. Install it once over USB so the new
two-slot partition table is present. After that, normal stable releases are checked nightly
over WiFi and installed without the Mac app or USB cable. Existing WiFi, Dexcom, Nightscout,
alert, and display settings remain in NVS during the migration and future updates.

Open the device's **Device** page to check manually, install an available update, enable or
disable automatic installs, or choose the local install hour. Automatic installation is
deferred for low battery, urgent glucose/notifications, an active buzzer or timer, setup/AP
mode, low heap, unavailable time, or lost WiFi. Manifest checks and firmware downloads use
certificate-validated HTTPS, and every release manifest is verified with the public release
key compiled into the firmware.

The bootloader writes updates to the inactive application slot. The new firmware must pass a
15-second local health check before it is marked valid; a crash, watchdog reset, or failed
health check causes the bootloader to restore the previous slot. Internet and CGM availability
are deliberately not part of that health check.

USB flashing remains the recovery path. See [INSTALL.md](INSTALL.md) for the non-erasing
bootstrap migration and recovery commands. Maintainers should follow
[docs/OTA_SIGNING.md](docs/OTA_SIGNING.md) for release signing and key rotation.

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Clock not detected via USB | Use a data cable (not charge-only), connect directly (no hub), install CH340 driver |
| Upload fails | Hold middle button while plugging in USB to enter flash mode |
| `NO WIFI` on display | Check SSID/password, make sure it's a 2.4 GHz network |
| `NO DATA` on display | Check Dexcom credentials or server URL on the config page |

See the **[Help & FAQ](https://sugarclock.com/support.html)** for more.

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
