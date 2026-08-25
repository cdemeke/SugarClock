#!/usr/bin/env python3
import os
import sys

try:
    Import("env")
    RUNNING_IN_PIO = True
except Exception:
    RUNNING_IN_PIO = False

MAX_SLOT_SIZE = 0x1C0000 # 1,835,008 bytes (1.75 MiB)
WARN_THRESHOLD = int(MAX_SLOT_SIZE * 0.85) # 1,559,756 bytes (85%)

def check_firmware_size(source, target, env):
    firmware_path = str(target[0])
    if not os.path.exists(firmware_path):
        return

    size = os.path.getsize(firmware_path)
    pct = (size / MAX_SLOT_SIZE) * 100.0
    print(f"[POST-BUILD] Firmware size: {size:,} bytes / {MAX_SLOT_SIZE:,} bytes ({pct:.1f}% of OTA slot)")

    if size > MAX_SLOT_SIZE:
        print(f"[ERROR] Firmware size {size:,} bytes exceeds maximum OTA slot size {MAX_SLOT_SIZE:,} bytes!")
        sys.exit(1)
    elif size > WARN_THRESHOLD:
        print(f"[WARNING] Firmware size is at {pct:.1f}% of OTA slot capacity. High slot utilization!")

if RUNNING_IN_PIO:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_firmware_size)
