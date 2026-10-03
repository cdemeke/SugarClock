#!/usr/bin/env python3
"""Prepare a preservation-first USB migration from a full flash backup. Never flashes.
The backup contains credentials. Keep it private. Requires the existing mklittlefs tool.
"""
import argparse
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile

FLASH_SIZE=0x400000
NEW_FS_SIZE=0x70000

def partitions(image):
    if len(image)!=FLASH_SIZE:
        raise ValueError('Expected an exact 4 MiB flash backup')
    result=[]
    for pos in range(0x8000,0x9000,32):
        entry=image[pos:pos+32]
        if entry[:2] in (b'\xff\xff',b'\xeb\xeb'):break
        magic,kind,subtype,offset,size,label,flags=struct.unpack('<HBBII16sI',entry)
        if magic!=0x50aa or offset+size>FLASH_SIZE or size==0:
            raise ValueError('Unrecognized or invalid partition table; do not flash')
        result.append(dict(kind=kind,subtype=subtype,offset=offset,size=size,label=label.rstrip(b'\0').decode('ascii'),flags=flags))
    nvs=[p for p in result if p['kind']==1 and p['subtype']==2]
    if len(nvs)!=1 or (nvs[0]['offset'],nvs[0]['size'])!=(0x9000,0x5000):
        raise ValueError('NVS layout is not compatible with preservation migration')
    return result

def prepare(backup,mklittlefs,output):
    image=pathlib.Path(backup).read_bytes()
    table=partitions(image)
    filesystems=[p for p in table if p['kind']==1 and p['subtype']==0x82]
    if len(filesystems)!=1:raise ValueError('Expected one existing LittleFS partition')
    fs=filesystems[0]
    out=pathlib.Path(output);out.mkdir(parents=True,exist_ok=True)
    out.chmod(0o700)
    with tempfile.TemporaryDirectory(prefix='sugarclock-migration-') as temp:
        temp=pathlib.Path(temp);old=temp/'old.bin';old.write_bytes(image[fs['offset']:fs['offset']+fs['size']]);old.chmod(0o600)
        tree=temp/'files';tree.mkdir()
        # Fail closed if the filesystem cannot be read. Never silently lose certificates.
        subprocess.run([mklittlefs,'-u',str(tree),'-s',str(fs['size']),'-b','4096','-p','256',str(old)],check=True,capture_output=True)
        # An unapplied installer overlay may contain stale credentials; preserve it as
        # a recovery file without replaying it over the already configured NVS values.
        overlay=tree/'config.json'
        if overlay.exists():overlay.rename(tree/'migration-config.json')
        target=out/'preserved-littlefs.bin'
        subprocess.run([mklittlefs,'-c',str(tree),'-s',str(NEW_FS_SIZE),'-b','4096','-p','256',str(target)],check=True,capture_output=True)
        target.chmod(0o600)
        metadata={'filesystem_source':fs,'target_size':NEW_FS_SIZE,'preserved_files':sorted(str(p.relative_to(tree)) for p in tree.rglob('*') if p.is_file()),'nvs_written':False,'flashed':False}
        (out/'migration.json').write_text(json.dumps(metadata,indent=2)+'\n')
    return target

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backup',required=True)
    parser.add_argument('--mklittlefs',required=True)
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    target=prepare(args.backup,args.mklittlefs,args.output)
    print('Prepared preserved filesystem:',target)
    print('No device was flashed. Follow docs/BLE_MIGRATION.md for reviewed USB commands.')
if __name__=='__main__':main()
