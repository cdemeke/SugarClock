#!/bin/bash
#
# download_tools.sh — Download esptool + mklittlefs and copy pre-built firmware
# Run this script once before building SugarClock Setup in Xcode.
# Usage: ./Scripts/download_tools.sh
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
RESOURCES_DIR="$PROJECT_DIR/TC001Setup/Resources"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"

# Target directories
TOOLS_DIR="$RESOURCES_DIR/Tools"
FIRMWARE_DIR="$RESOURCES_DIR/Firmware"
WEBUI_DIR="$RESOURCES_DIR/WebUI/www"

# Versions
ESPTOOL_VERSION="v4.8.1"
MKLITTLEFS_VERSION="3.2.0"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    ESPTOOL_PLATFORM="macos-arm64"
else
    ESPTOOL_PLATFORM="macos-amd64"
fi

echo "=== SugarClock Setup Resource Downloader ==="
echo "Project dir: $PROJECT_DIR"
echo "Repo root:   $REPO_ROOT"
echo "Architecture: $ARCH"
echo ""

# Create directories
mkdir -p "$TOOLS_DIR" "$FIRMWARE_DIR" "$WEBUI_DIR"

# ── 1. Download esptool ──────────────────────────────────────────────
echo "--- Downloading esptool $ESPTOOL_VERSION ---"
ESPTOOL_URL="https://github.com/espressif/esptool/releases/download/${ESPTOOL_VERSION}/esptool-${ESPTOOL_VERSION}-${ESPTOOL_PLATFORM}.zip"
ESPTOOL_TMP="/tmp/esptool_download.zip"

if [ -f "$TOOLS_DIR/esptool" ]; then
    echo "esptool already exists, skipping download."
else
    echo "Downloading from: $ESPTOOL_URL"
    curl -L -o "$ESPTOOL_TMP" "$ESPTOOL_URL"
    ESPTOOL_EXTRACT="/tmp/esptool_extract"
    rm -rf "$ESPTOOL_EXTRACT"
    unzip -q "$ESPTOOL_TMP" -d "$ESPTOOL_EXTRACT"
    # Find the esptool binary in the extracted contents
    ESPTOOL_BIN=$(find "$ESPTOOL_EXTRACT" -name "esptool" -type f | head -1)
    if [ -z "$ESPTOOL_BIN" ]; then
        echo "ERROR: Could not find esptool binary in archive"
        exit 1
    fi
    cp "$ESPTOOL_BIN" "$TOOLS_DIR/esptool"
    chmod 755 "$TOOLS_DIR/esptool"
    rm -rf "$ESPTOOL_TMP" "$ESPTOOL_EXTRACT"
    echo "esptool installed to $TOOLS_DIR/esptool"
fi

# ── 2. Download mklittlefs ───────────────────────────────────────────
echo ""
echo "--- Downloading mklittlefs $MKLITTLEFS_VERSION ---"

if [ "$ARCH" = "arm64" ]; then
    MKLITTLEFS_SUFFIX="darwin-arm64"
else
    MKLITTLEFS_SUFFIX="darwin-amd64"
fi
MKLITTLEFS_URL="https://github.com/earlephilhower/mklittlefs/releases/download/${MKLITTLEFS_VERSION}/mklittlefs-${MKLITTLEFS_SUFFIX}.tar.gz"
MKLITTLEFS_TMP="/tmp/mklittlefs_download.tar.gz"

if [ -f "$TOOLS_DIR/mklittlefs" ]; then
    echo "mklittlefs already exists, skipping download."
else
    echo "Downloading from: $MKLITTLEFS_URL"
    curl -L -o "$MKLITTLEFS_TMP" "$MKLITTLEFS_URL"
    MKLITTLEFS_EXTRACT="/tmp/mklittlefs_extract"
    rm -rf "$MKLITTLEFS_EXTRACT"
    mkdir -p "$MKLITTLEFS_EXTRACT"
    tar -xzf "$MKLITTLEFS_TMP" -C "$MKLITTLEFS_EXTRACT"
    MKLITTLEFS_BIN=$(find "$MKLITTLEFS_EXTRACT" -name "mklittlefs" -type f | head -1)
    if [ -z "$MKLITTLEFS_BIN" ]; then
        echo "ERROR: Could not find mklittlefs binary in archive"
        exit 1
    fi
    cp "$MKLITTLEFS_BIN" "$TOOLS_DIR/mklittlefs"
    chmod 755 "$TOOLS_DIR/mklittlefs"
    rm -rf "$MKLITTLEFS_TMP" "$MKLITTLEFS_EXTRACT"
    echo "mklittlefs installed to $TOOLS_DIR/mklittlefs"
fi

# ── 3. Copy pre-built firmware binaries ──────────────────────────────
echo ""
echo "--- Copying pre-built firmware binaries ---"

PIO_BUILD="$REPO_ROOT/.pio/build/esp32dev"

if [ -d "$PIO_BUILD" ]; then
    for BIN in bootloader.bin partitions.bin firmware.bin; do
        if [ -f "$PIO_BUILD/$BIN" ]; then
            cp "$PIO_BUILD/$BIN" "$FIRMWARE_DIR/$BIN"
            echo "Copied $BIN"
        else
            echo "WARNING: $PIO_BUILD/$BIN not found. Build firmware first with: pio run"
        fi
    done
else
    echo "WARNING: PlatformIO build directory not found at $PIO_BUILD"
    echo "         Build firmware first with: cd $REPO_ROOT && pio run"
fi

# ── 4. Copy boot_app0.bin ────────────────────────────────────────────
echo ""
echo "--- Looking for boot_app0.bin ---"

BOOT_APP0=""
# Search in PlatformIO packages
if [ -d "$HOME/.platformio/packages" ]; then
    BOOT_APP0=$(find "$HOME/.platformio/packages" -name "boot_app0.bin" -path "*/partitions/*" 2>/dev/null | head -1)
fi

if [ -n "$BOOT_APP0" ] && [ -f "$BOOT_APP0" ]; then
    cp "$BOOT_APP0" "$FIRMWARE_DIR/boot_app0.bin"
    echo "Copied boot_app0.bin from $BOOT_APP0"
else
    echo "WARNING: boot_app0.bin not found in PlatformIO packages."
    echo "         Install ESP32 platform first: pio platform install espressif32"
fi

# ── 5. Copy web UI files ─────────────────────────────────────────────
echo ""
echo "--- Copying web UI files ---"

DATA_WWW="$REPO_ROOT/data/www"
if [ -d "$DATA_WWW" ]; then
    cp -R "$DATA_WWW/"* "$WEBUI_DIR/"
    echo "Copied web UI files from $DATA_WWW"
    ls -la "$WEBUI_DIR/"
else
    echo "WARNING: Web UI directory not found at $DATA_WWW"
fi

# ── Summary ───────────────────────────────────────────────────────────
echo ""
echo "=== Summary ==="
echo "Tools:"
ls -la "$TOOLS_DIR/" 2>/dev/null || echo "  (empty)"
echo ""
echo "Firmware:"
ls -la "$FIRMWARE_DIR/" 2>/dev/null || echo "  (empty)"
echo ""
echo "Web UI:"
ls -la "$WEBUI_DIR/" 2>/dev/null || echo "  (empty)"
echo ""
echo "Done! You can now build SugarClock Setup in Xcode."
