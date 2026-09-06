#!/usr/bin/env python3
"""Capture production SwiftUI screens using explicit, labeled simulator fixtures."""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--app', required=True, type=Path, help='Built Debug iOS Simulator .app')
parser.add_argument('--device', required=True, help='Booted simulator UUID')
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'docs/screenshots')
parser.add_argument('--simctl', help='Optional installed simctl binary when the Xcode wrapper cannot run')
args = parser.parse_args()
app = args.app.resolve()
with (app / 'Info.plist').open('rb') as file:
    info = plistlib.load(file)
if 'iPhoneSimulator' not in info.get('CFBundleSupportedPlatforms', []):
    parser.error('Only iOS Simulator app bundles can be used')
identifier = info['CFBundleIdentifier']
command = [args.simctl] if args.simctl else ['xcrun', 'simctl']

def sim(*parts, check=True, env=None):
    return subprocess.run(command + list(parts), check=check, env=env, timeout=60)

# Keep original branding available in the local simulator bundle when actool is
# blocked. Normal project builds still use the checked-in asset catalog.
assets = Path(__file__).resolve().parent / 'SugarClock/Assets.xcassets/AppLogo.imageset'
for source, dest in [('logo.png', 'AppLogo.png'), ('logo@2x.png', 'AppLogo@2x.png')]:
    shutil.copy2(assets / source, app / dest)
sim('install', args.device, str(app))
sim('status_bar', args.device, 'override', '--time', '9:41', '--dataNetwork', 'wifi',
    '--wifiMode', 'active', '--wifiBars', '3', '--batteryState', 'charged', '--batteryLevel', '100')
args.output.mkdir(parents=True, exist_ok=True)
screens = [('01-my-clocks', 'clocks'), ('02-device-settings', 'device'), ('03-wifi', 'wifi'),
           ('04-brightness', 'brightness'), ('05-configured-secret', 'secret'),
           ('06-firmware', 'firmware'), ('07-troubleshooting', 'troubleshooting'),
           ('08-firmware-dark', 'firmware')]
for filename, screen in screens:
    sim('terminate', args.device, identifier, check=False)
    sim('ui', args.device, 'appearance', 'dark' if filename.endswith('dark') else 'light')
    env = dict(os.environ, SIMCTL_CHILD_SUGARCLOCK_SCREENSHOT=screen)
    sim('launch', args.device, identifier, env=env)
    time.sleep(4)
    sim('io', args.device, 'screenshot', str((args.output / (filename + '.png')).resolve()))
sim('terminate', args.device, identifier)
sim('ui', args.device, 'appearance', 'light')
sim('status_bar', args.device, 'clear')
print(f'Captured {len(screens)} labeled screenshots in {args.output.resolve()}')
