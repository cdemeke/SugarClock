# SugarClock — Installation Guide

## Prerequisites

### Hardware
- Ulanzi TC001 smart pixel clock
- USB-C data cable (the included cable can be flaky — use a quality one)
- Computer with a free USB port (connect directly, not through a hub)

### Software
- [PlatformIO CLI](https://platformio.org/install/cli) or PlatformIO IDE (VS Code extension)
- CH340 USB-to-serial driver (see below)

### CH340 Driver Installation

The Ulanzi TC001 uses a CH340 USB-to-serial chip.

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
pip install -r requirements-build.txt

# Build firmware
pio run
```

Expected output:
```
RAM:   [==        ]  15.9% (used 52084 bytes from 327680 bytes)
Flash: [======    ]  XX.X% (used XXXXXXX bytes from 2097152 bytes)
========================= [SUCCESS] Took X seconds =========================
```

Build the web UI filesystem image:
```bash
pio run --target buildfs
```

---

## Step 3: Connect the Ulanzi TC001

1. Plug the USB-C cable into the Ulanzi TC001 and your computer
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
2. Hold the **middle button** on the Ulanzi TC001
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
# For a brand-new device only (this destroys all settings):
esptool.py -p /dev/cu.usbserial-1410 erase_flash

# Flash firmware
esptool.py -p /dev/cu.usbserial-1410 -b 460800 write_flash \
  0x1000  .pio/build/esp32dev/bootloader.bin \
  0x8000  .pio/build/esp32dev/partitions.bin \
  0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/esp32dev/firmware.bin \
  0x390000 .pio/build/esp32dev/littlefs.bin
```

### One-time OTA/Bluetooth migration (preserve existing settings)

For a configured clock, use the Mac installer's **Upgrade existing SugarClock**
option or the backup/extract/repack procedure in [BLE migration](docs/BLE_MIGRATION.md).
Do not run `erase_flash`. NVS remains at `0x9000`, but avoiding erase alone does
not preserve LittleFS overlays or enterprise CA certificates when its layout changes.
The preservation flow validates the actual partition table, keeps a private full-flash
backup, and rebuilds the existing filesystem before any write. It stops if preservation
fails. Do not use a fresh empty filesystem image for this migration.

After installing Bluetooth-capable firmware, pair using the [iOS companion](ios/README.md).
A configured clock requires holding only the middle button for three seconds, then
releasing. Existing configuration is read from the clock without re-entering secrets.


---

## Step 5: Verify the Flash

Open the serial monitor to confirm the device boots correctly:

```bash
pio device monitor
```

You should see:
```
================================
SugarClock v0.2.0
Reset reason: 1
================================
[CONFIG] No valid config found, writing defaults
[CONFIG] Poll interval: 60s, Brightness: 40
[BOOT] Setup complete
[BOOT] Free heap: XXXXX bytes
```

The LED matrix should briefly show `SUGAR` in teal, then transition to `SETUP` (indicating no WiFi/server is configured yet).

---

## Step 6: Configure via Web UI

### First-Time WiFi Setup

On first boot with no WiFi configured, the clock starts an open network named
`SugarClock-Setup`. The display cycles the network name and `192.168.4.1`.

1. Join `SugarClock-Setup` from a phone or computer.
2. If a captive-network window appears, follow its signpost and open a normal Safari,
   Chrome, or Edge window. The setup process is too long-lived for the captive webview.
3. Open `http://192.168.4.1`.
4. Open the **WiFi** tab, choose a visible 2.4 GHz network (or type a hidden SSID), and
   select **Join this network**.
5. The clock tests the credentials first and saves them only after the connection succeeds.

If saved WiFi cannot connect, the same setup network starts automatically after about two
minutes. The clock continues retrying the saved network whenever no phone is using the setup
page.

### School and WPA2-Enterprise WiFi

SugarClock supports WPA2-Enterprise with PEAP or EAP-TTLS username/password authentication.
It does not support certificate-based EAP-TLS.

For the easiest on-site flow, choose **Skip — I’ll set this up on the device** in the macOS
installer. At the school, power the clock, join `SugarClock-Setup` from a phone, open
`http://192.168.4.1`, and select the school network. Enterprise networks are marked in the
network list. PEAP is the right default for most K-12 networks; use EAP-TTLS only when school
IT specifies it.

Enter the identity exactly as IT provides it. Common formats are:

- `user@district.org`
- `DISTRICT\user`
- `user`

A correct password with the wrong identity format will still be rejected. Anonymous identity
is optional under **Advanced**.

By default, the clock trusts the authentication server certificate, equivalent to accepting a
certificate prompt on a phone. For stronger verification, paste the school CA certificate in
PEM format under **Advanced** before joining. When a CA is loaded, SugarClock validates the
network against it.

School IT can copy this outbound allowlist (also allow normal DNS resolution):

```text
TCP 443: share2.dexcom.com
TCP 443: shareous1.dexcom.com
TCP 443: api.openweathermap.org
TCP 443: the configured Nightscout host (when used)
TCP 443: github.com
TCP 443: release-assets.githubusercontent.com
UDP 123: pool.ntp.org
UDP 123: time.nist.gov
UDP 123: time.google.com
```

After association, the WiFi page reports DNS, data-source HTTPS, and NTP reachability
separately. This helps distinguish bad credentials from a school firewall that admits the
device but blocks the services it needs.

Hotel-style captive login pages are not supported: the clock has no browser and each portal
works differently. Use a travel router or phone hotspot for those networks.

### Finding the Web UI After Connection

Once connected to WiFi, the serial monitor shows the device's IP address:
```
[WIFI] Connected! IP: 192.168.1.xxx, RSSI: -45 dBm
[WEB] Server started at http://192.168.1.xxx/
```

Open that IP in a browser to access the web UI.

### Web UI Pages

| Page | URL | Purpose |
|------|-----|---------|
| Status | `/display.html` | Live glucose reading, trend, data age |
| Config | `/` | WiFi, server URL, thresholds, display settings |
| Debug | `/debug.html` | HTTP responses, heap usage, sensor data |
| Device | `/device.html` | Firmware info, restart/reset buttons |

The Device page also shows installed and available firmware versions, check status, download
progress, the nightly schedule, and manual **Check now** / **Install now** controls.

## USB recovery

OTA never modifies the bootloader or partition table and never exposes a firmware-upload web
route. If both application slots are damaged during development, enter flash mode (hold the
middle button while connecting USB) and repeat the complete five-segment `write_flash`
command above. Do not erase first when attempting to preserve NVS. A factory backup can be
restored at address `0x0` if the original Ulanzi firmware is desired.

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

| Button | Short Press (<1s) | Double Press | Long Press (>1s) |
|--------|-------------------|--------------|------------------|
| Left | Toggle display mode | — | Reset display overrides |
| Middle | Cycle brightness (10→40→100→200) | Show the clock's browser address | Release after 1–3s: snooze; 3–10s: Bluetooth pairing; 10s or longer: reset Bluetooth bonds |
| Right | Previous display / context action | — | Reset display / context action |

---

The middle-button hold actions happen on release. Hold only the middle button for Bluetooth controls. The outer-button combination is a TC001 power shortcut and must not be used for pairing. Bluetooth bond reset preserves Wi-Fi and application settings.

## Display States

| State | Color | Meaning |
|-------|-------|---------|
| Glucose number | Green | In range (80-180 mg/dL default) |
| Glucose number | Orange | Low (70-80) or High (180-250) |
| Glucose number | Red | Urgent low (<70) or Urgent high (>250) |
| Dimmed + `!` | Yellow | Data 10-20 minutes old (warning) |
| Glucose number | Gray | Data >20 min old or 5+ fetch failures (stale) |
| `NO DATA` | Red | 10+ failures or never received data |
| `NO WIFI` | Red | WiFi disconnected |
| Setup AP name / `192.168.4.1` | Teal | On-device WiFi setup is available |
| Connection failure detail | Teal | A trial WiFi join needs attention |
| Network limitation detail | Orange | Associated, but DNS, data HTTPS, or NTP is blocked |

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
