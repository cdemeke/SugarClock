#!/usr/bin/env python3
"""Stage local USB artifacts or update an unsigned local Mac app. Never flashes/publishes."""
import argparse
import hashlib
import json
import pathlib
import shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]
def package(output,app=None):
    build=ROOT/'.pio/build/esp32dev'
    files={name:build/name for name in ('firmware.bin','bootloader.bin','partitions.bin','littlefs.bin')}
    files['boot_app0.bin']=pathlib.Path.home()/'.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
    for name,path in files.items():
        if not path.is_file():raise ValueError(f'Missing {name}: build firmware and buildfs first')
    if files['firmware.bin'].stat().st_size>0x1c0000:raise ValueError('Firmware exceeds OTA slot')
    if files['littlefs.bin'].stat().st_size!=0x70000:raise ValueError('Filesystem has wrong size')
    target=pathlib.Path(output);target.mkdir(parents=True,exist_ok=True)
    metadata={'version':(ROOT/'VERSION').read_text().strip(),'protocol_major':1,'sha256':{}}
    for name,path in files.items():
        shutil.copy2(path,target/name)
        metadata['sha256'][name]=hashlib.sha256(path.read_bytes()).hexdigest()
    (target/'version.txt').write_text(metadata['version']+'\n')
    (target/'build-artifacts.json').write_text(json.dumps(metadata,indent=2)+'\n')
    shutil.copy2(ROOT/'protocol/compatibility.json',target/'mobile-protocol.json')
    if app:
        bundle=pathlib.Path(app)
        if bundle.suffix!='.app' or not (bundle/'Contents/Info.plist').exists():raise ValueError('Expected an existing local Mac .app bundle')
        dest=bundle/'Contents/Resources/Firmware';dest.mkdir(parents=True,exist_ok=True)
        for path in target.iterdir():
            if path.is_file():shutil.copy2(path,dest/path.name)
    return metadata
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True)
    parser.add_argument('--app',help='Optional unsigned local Mac app; do not modify a distributed/signed bundle')
    args=parser.parse_args()
    print(json.dumps(package(args.output,args.app),indent=2))
