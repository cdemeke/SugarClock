# TC001 Glucose Display — Installation Guide

## Prerequisites

### Hardware
- Ulanzi TC001 smart pixel clock
- USB-C data cable (the included cable can be flaky — use a quality one)
- Computer with a free USB port (connect directly, not through a hub)

### Software
- [PlatformIO CLI](https://platformio.org/install/cli) or PlatformIO IDE (VS Code extension)
- CH340 USB-to-serial driver (see below)

### CH340 Driver Installation

The TC001 uses a CH340 USB-to-serial chip.

**macOS**: Usually works out of the box on 10.9+. If not detected, install the [CH340 driver](https://sparks.gogo.co.nz/ch340.html).

**Windows**: Download and run the [CH340 driver installer](https://sparks.gogo.co.nz/ch340.html). After installation, the device appears as `USB-SERIAL CH340 (COM##)` in Device Manager.

**Linux**: Built into the kernel on most distributions. No installation needed.

---

## Step 1: Backup Factory Firmware (Recommended)

Before flashing custom firmware, back up the stock firmware so you can restore it later.

```bash
# Install esptool if you don't have it
pip install esptool

# Find your serial port
# macOS:  /dev/cu.usbserial-*  or  /dev/cu.wchusbserial-*
# Linux:  /dev/ttyUSB0
# Windows: COM3, COM4, etc. (check Device Manager)

# Read full 8MB flash (takes ~2 minutes)
esptool.py -p /dev/cu.usbserial-1410 -b 921600 read_flash 0x0 0x800000 tc001_factory_backup.bin
```

Keep `tc001_factory_backup.bin` somewhere safe.

---

## Step 2: Build the Firmware

Clone or download this project, then build:

```bash
cd tc001

# Install PlatformIO if needed
pip install platformio

# Build firmware
pio run
```

Expected output:
```
RAM:   [==        ]  15.9% (used 52084 bytes from 327680 bytes)
Flash: [====      ]  35.4% (used 1112977 bytes from 3145728 bytes)
========================= [SUCCESS] Took X seconds =========================
```

Build the web UI filesystem image:
```bash
pio run --target buildfs
```

---

## Step 3: Connect the TC001

1. Plug the USB-C cable into the TC001 and your computer
2. The device should power on and the serial port should appear

Verify the connection:
```bash
# macOS/Linux
ls /dev/cu.* /dev/ttyUSB* 2>/dev/null

# Windows (PowerShell)
Get-WMIObject Win32_SerialPort | Select-Object DeviceID, Description
```

### Entering Flash Mode (if needed)

If the upload fails with a connection error:
1. Unplug the USB cable
2. Hold the **middle button** on the TC001
3. Plug in the USB cable while holding the button
4. Release after 1-2 seconds
5. Retry the upload

---

## Step 4: Flash the Firmware

### Option A: PlatformIO (Recommended)

```bash
# Upload firmware
pio run --target upload

# Upload web UI filesystem
pio run --target uploadfs
```

If PlatformIO can't auto-detect the port, specify it:
```bash
pio run --target upload --upload-port /dev/cu.usbserial-1410
pio run --target uploadfs --upload-port /dev/cu.usbserial-1410
```

### Option B: esptool.py (Manual)

```bash
# Erase flash first (clean start)
esptool.py -p /dev/cu.usbserial-1410 erase_flash

# Flash firmware
esptool.py -p /dev/cu.usbserial-1410 -b 460800 write_flash \
  0x1000  .pio/build/esp32dev/bootloader.bin \
  0x8000  .pio/build/esp32dev/partitions.bin \
  0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/esp32dev/firmware.bin \
  0x310000 .pio/build/esp32dev/littlefs.bin
```

---

## Step 5: Verify the Flash

Open the serial monitor to confirm the device boots correctly:

```bash
pio device monitor
```

You should see:
```
================================
TC001 Glucose Display v1.0.0
Reset reason: 1
================================
[CONFIG] No valid config found, writing defaults
[CONFIG] Poll interval: 60s, Brightness: 40
[BOOT] Setup complete
[BOOT] Free heap: XXXXX bytes
```

The LED matrix should briefly show `TC001` in teal, then transition to `SETUP` (indicating no WiFi/server is configured yet).

---

## Step 6: Configure via Web UI

### First-Time WiFi Setup

On first boot with no WiFi configured, the display shows `SETUP`. You'll need to configure WiFi credentials. There are two ways:

#### Option A: Serial Configuration (First Boot)

Connect via serial monitor and use the web UI after connecting to WiFi. For initial setup, you can pre-configure WiFi before flashing by editing `src/config_manager.cpp` and changing the defaults:

```cpp
// In config_set_defaults(), change:
strncpy(config.wifi_ssid, "YourNetworkName", sizeof(config.wifi_ssid));
strncpy(config.wifi_password, "YourPassword", sizeof(config.wifi_password));
```

Then rebuild and reflash. The device will connect to your WiFi automatically.

#### Option B: Web UI Configuration

Once connected to WiFi, the serial monitor shows the device's IP address:
```
[WIFI] Connected! IP: 192.168.1.xxx, RSSI: -45 dBm
[WEB] Server started at http://192.168.1.xxx/
```

Open that IP in a browser to access the web UI.

### Web UI Pages

| Page | URL | Purpose |
|------|-----|---------|
| Status | `/` | Live glucose reading, trend, data age |
| Config | `/config.html` | WiFi, server URL, thresholds, display settings |
| Debug | `/debug.html` | HTTP responses, heap usage, sensor data |
| Device | `/device.html` | Firmware info, restart/reset buttons |

### Required Configuration

Navigate to the **Config** page and set:

1. **Server URL** — Your glucose data endpoint (HTTPS supported)
2. **Auth Token** — Bearer token for the server (if required)
3. **Poll Interval** — How often to fetch data (minimum 15 seconds)
4. **Glucose Thresholds** — Customize color ranges for your needs

The server endpoint should return JSON in this format:
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

---

## Button Controls

| Button | Short Press (<1s) | Long Press (>1s) |
|--------|-------------------|------------------|
| Left | Toggle glucose / time display | Reset display overrides |
| Middle | Cycle brightness (10→40→100→200) | Reset display overrides |
| Right | — | Reset display overrides |

---

## Display States

| State | Color | Meaning |
|-------|-------|---------|
| Glucose number | Green | In range (80-180 mg/dL default) |
| Glucose number | Orange | Low (70-80) or High (180-250) |
| Glucose number | Red | Urgent low (<70) or Urgent high (>250) |
| Dimmed + `!` | Yellow | Data 10-20 minutes old |
| `STALE` | Yellow | Data >20 min old or 5+ fetch failures |
| `NO DATA` | Red | 10+ failures or never received data |
| `NO WIFI` | Red | WiFi disconnected |
| `SETUP` | White | No WiFi/server configured |

---

## Troubleshooting

### Device not detected via USB
- Try a different USB-C cable (must be a data cable, not charge-only)
- Connect directly to your computer, not through a USB hub
- Install/reinstall CH340 drivers
- Try a different USB port

### Upload fails with timeout
- Hold the middle button while plugging in USB to enter flash mode
- Try lowering upload speed: add `upload_speed = 115200` to `platformio.ini`

### Display stays on SETUP
- Configure WiFi credentials via the Config page or by hardcoding in source

### Display shows NO WIFI
- Check WiFi credentials in the Config page
- Verify your WiFi network is 2.4GHz (ESP32 does not support 5GHz)
- Check signal strength — move the device closer to your router

### Display shows NO DATA
- Verify the server URL is correct and reachable
- Check the auth token if your server requires one
- Visit the Debug page to see the last HTTP response code and body

### Watchdog resets (device keeps rebooting)
- Connect serial monitor to see reset reason
- Check for network issues causing long blocking operations

---

## Restoring Factory Firmware

If you backed up the factory firmware in Step 1:

```bash
esptool.py -p /dev/cu.usbserial-1410 -b 460800 write_flash 0x0 tc001_factory_backup.bin
```

---

## Updating Firmware

To update to a new version:

```bash
cd tc001
git pull              # if using git
pio run --target upload    # flash firmware
pio run --target uploadfs  # flash web UI (if changed)
```

Your configuration is stored in NVS (non-volatile storage) and persists across firmware updates. Only a factory reset erases settings.
