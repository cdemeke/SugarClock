# BG_TC001 — Glucose Monitor for Ulanzi TC001

Custom firmware that turns the [Ulanzi TC001 Smart Pixel Clock](https://www.amazon.com/ULANZI-TC001-Smart-Pixel-Clock/dp/B0CXX91TY5) (~$40–50) into a real-time continuous glucose monitor (CGM) display. Connects to Dexcom Share or a custom server to show your blood glucose, trend arrows, and alerts on an 8x32 RGB LED matrix — with a full web UI for configuration.

**Cost:** ~$40–50 one-time (the Ulanzi TC001) + ~1 hour of setup time. Compare to the [SugarPixel](https://customtypeone.com/products/sugarpixel) (~$100) which does something similar via a mobile app.

![ESP32](https://img.shields.io/badge/ESP32-WROOM--32D-blue)
![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino-orange)
![License](https://img.shields.io/badge/license-MIT-green)

## Features

- **Live Glucose Display** — Large color-coded numbers on the LED matrix with trend arrows
- **Dexcom Share Integration** — Direct connection to Dexcom Share (US & International)
- **Custom Server Support** — Connect to any JSON API endpoint (Nightscout, etc.)
- **Weather Display** — Optional current temperature via OpenWeatherMap
- **Web Dashboard** — Real-time glucose, trend graph, and full configuration from any browser
- **Customizable Alerts** — Buzzer alerts for high/low glucose with snooze
- **Theme Colors** — Full RGB color customization for each glucose range
- **Night Mode** — Automatic brightness reduction during sleeping hours
- **Auto-Brightness** — LDR sensor adjusts display to ambient light
- **Button Controls** — Cycle views, adjust brightness, snooze alerts

## Hardware

| Component | Specification |
|-----------|--------------|
| Board | Ulanzi TC001 (ESP32-WROOM-32D @ 240 MHz) |
| Display | 8x32 WS2812B RGB LED matrix (256 LEDs) |
| Flash | 4 MB |
| Buttons | 3x mechanical (left, middle, right) |
| Sensors | LDR (ambient light), battery voltage ADC |
| Buzzer | Piezo on GPIO 15 |
| Connectivity | WiFi 802.11 b/g/n (2.4 GHz) |
| USB | USB-C (CH340 serial) |

### Pin Map

| Function | GPIO |
|----------|------|
| LED Matrix Data | 32 |
| Button Left | 26 |
| Button Middle | 27 |
| Button Right | 14 |
| Buzzer | 15 |
| LDR (Light) | 35 |
| Battery ADC | 34 |
| I2C SDA | 21 |
| I2C SCL | 22 |

## Display States

| Display | Color | Meaning |
|---------|-------|---------|
| Glucose number | Green | In range (80–180 mg/dL) |
| Glucose number | Orange | Low (70–80) or High (180–250) |
| Glucose number | Red | Urgent low (<70) or Urgent high (>250) |
| Dimmed + `!` | Yellow | Data is 10–20 minutes old |
| `STALE` | Yellow | Data >20 min old or repeated fetch failures |
| `72*F` | Cyan | Weather display (temperature) |
| `12:30` | Cyan | Time display |
| `NO DATA` | Red | Cannot retrieve glucose data |
| `NO WIFI` | Red | WiFi disconnected |
| `SETUP` | White | No WiFi/server configured |

## Button Controls

| Button | Short Press | Long Press |
|--------|-------------|------------|
| Left | Cycle display: Glucose → Time → Weather | Clear overrides |
| Middle | Cycle brightness: 10 → 40 → 100 → 200 | Snooze alerts |
| Right | — | Clear overrides |

## Getting Started

### Prerequisites

- [PlatformIO CLI](https://platformio.org/install/cli) or VS Code with PlatformIO extension
- CH340 USB-to-serial driver ([download](https://sparks.gogo.co.nz/ch340.html) — macOS 10.9+ and Linux usually have it built-in)
- USB-C data cable

### Build & Flash

```bash
# Clone the repo
git clone https://github.com/cdemeke/BG_TC001.git
cd BG_TC001

# Build firmware
pio run

# Build web UI filesystem
pio run --target buildfs

# Flash firmware (connect TC001 via USB-C)
pio run --target upload

# Flash web UI
pio run --target uploadfs
```

If upload fails, hold the **middle button** while plugging in USB to enter flash mode.

### First-Time Setup

1. On first boot the display shows `SETUP`
2. Pre-configure WiFi by editing defaults in `src/config_manager.cpp`:
   ```cpp
   strncpy(config.wifi_ssid, "YourNetwork", sizeof(config.wifi_ssid));
   strncpy(config.wifi_password, "YourPassword", sizeof(config.wifi_password));
   ```
3. Rebuild and flash, then check the serial monitor for the device IP:
   ```
   [WIFI] Connected! IP: 192.168.1.xxx
   [WEB] Server started at http://192.168.1.xxx/
   ```
4. Open that IP in a browser to access the web dashboard

### Configuration

Navigate to `http://<device-ip>/config.html` to set up:

- **General** — WiFi, data source (Custom URL or Dexcom Share), poll interval, timezone
- **Display** — Brightness, default view, night mode, glucose units (mg/dL or mmol/L)
- **Alerts** — Buzzer thresholds, snooze duration
- **Theme** — Custom RGB colors for each glucose range
- **Weather** — OpenWeatherMap API key, city, temperature unit, update interval

## Data Sources

### Dexcom Share

Select "Dexcom Share" in the data source dropdown, enter your Dexcom username/password, and choose US or International region. The device handles the full authentication flow automatically.

### Custom URL

Point to any HTTPS endpoint that returns JSON:

```json
{
  "glucose": 125,
  "trend": "Flat",
  "timestamp": 1708000000,
  "message": "",
  "force_mode": -1
}
```

Supported trend values: `RisingFast`, `Rising`, `Flat`, `Falling`, `FallingFast`

Optional Bearer token authentication is supported.

### Weather (Optional)

Enable weather in the config to add temperature to the display toggle cycle. Requires a free [OpenWeatherMap API key](https://openweathermap.org/appid). Displays temperature in Fahrenheit or Celsius on the LED matrix in cyan.

## Web UI

| Page | Path | Description |
|------|------|-------------|
| Dashboard | `/` | Live glucose, trend arrow, delta, history graph |
| Config | `/config.html` | All device settings (5 tabs) |
| Debug | `/debug.html` | HTTP responses, heap usage, sensor data, raw readings |
| Device | `/device.html` | Firmware info, MAC address, restart/factory reset |

### API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | Current glucose, trend, state, WiFi info |
| GET | `/api/config` | All configuration (passwords masked) |
| POST | `/api/config` | Update configuration |
| GET | `/api/debug` | System diagnostics |
| GET | `/api/history` | Last 48 glucose readings |
| POST | `/api/restart` | Restart device |
| POST | `/api/factory-reset` | Reset to defaults and restart |

## Project Structure

```
BG_TC001/
├── platformio.ini           # Build configuration & dependencies
├── partitions_custom.csv    # ESP32 flash partition layout
├── include/                 # Header files
│   ├── hardware_pins.h      # GPIO definitions
│   ├── config_manager.h     # Configuration struct & NVS
│   ├── display.h            # LED matrix interface
│   ├── glucose_engine.h     # Display state machine
│   ├── http_client.h        # Glucose data fetching
│   ├── weather_client.h     # OpenWeatherMap client
│   ├── web_server.h         # Async HTTP server
│   ├── wifi_manager.h       # WiFi connection management
│   ├── time_engine.h        # NTP time sync
│   ├── sensors.h            # LDR & battery monitoring
│   ├── buttons.h            # Debounced button input
│   └── trend_arrows.h       # Trend arrow bitmaps
├── src/                     # Implementation files
│   ├── main.cpp             # Setup & main loop
│   └── *.cpp                # One per header
└── data/www/                # Web UI (served via LittleFS)
    ├── index.html           # Dashboard
    ├── config.html          # Configuration
    ├── debug.html           # Debug diagnostics
    ├── device.html          # Device info
    └── style.css            # Dark theme stylesheet
```

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [FastLED](https://github.com/FastLED/FastLED) | ^3.6.0 | WS2812B LED control |
| [FastLED NeoMatrix](https://github.com/marcmerlin/FastLED_NeoMatrix) | ^1.2 | 2D matrix abstraction |
| [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) | ^1.11.9 | Text & graphics rendering |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.0.0 | JSON parsing |
| [ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer) | ^3.1.0 | Non-blocking web server |
| [AsyncTCP](https://github.com/mathieucarbou/AsyncTCP) | ^3.1.0 | Async networking |

All dependencies are managed by PlatformIO and installed automatically on first build.

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Device not detected via USB | Use a data cable (not charge-only), connect directly (no hub), install CH340 driver |
| Upload fails with timeout | Hold middle button while plugging in USB to enter flash mode |
| `SETUP` on display | Configure WiFi in source code or web UI |
| `NO WIFI` | Verify SSID/password, ensure 2.4 GHz network (5 GHz not supported) |
| `NO DATA` | Check server URL / Dexcom credentials on Debug page |
| Watchdog resets | Check serial monitor for reset reason, look for network issues |

See [INSTALL.md](INSTALL.md) for detailed installation and flashing instructions.

## Backup & Restore Factory Firmware

```bash
# Backup (before flashing custom firmware)
esptool.py -p /dev/cu.usbserial-* -b 921600 read_flash 0x0 0x800000 tc001_factory_backup.bin

# Restore
esptool.py -p /dev/cu.usbserial-* -b 460800 write_flash 0x0 tc001_factory_backup.bin
```

## Where to Buy

- [Ulanzi TC001 Smart Pixel Clock — Amazon US](https://www.amazon.com/ULANZI-TC001-Smart-Pixel-Clock/dp/B0CXX91TY5) (~$40–50)
- Use a **high-quality USB-C data cable** — the included cable can be flaky

## Acknowledgments

- [AWTRIX3](https://blueforcer.github.io/awtrix3/#/) — The original LED matrix firmware for the TC001 that inspired this project
- [pydexcom](https://github.com/gagebenne/pydexcom) — Dexcom Share API reference
- [OpenWeatherMap](https://openweathermap.org/) — Free weather API

## License

MIT
