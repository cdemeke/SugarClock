import pathlib
import struct
import sys
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import prepare_ble_migration as migration
class MigrationTests(unittest.TestCase):
    def image(self,offset=0x210000,size=0x1f0000):
        data=bytearray(b'\xff'*0x400000)
        for i,(kind,sub,off,n,label) in enumerate([(1,2,0x9000,0x5000,b'nvs'),(1,0x82,offset,size,b'spiffs')]):
            data[0x8000+i*32:0x8020+i*32]=struct.pack('<HBBII16sI',0x50aa,kind,sub,off,n,label,0)
        return data
    def test_old_and_current_layout(self):
        for offset,size in [(0x210000,0x1f0000),(0x390000,0x70000)]:
            table=migration.partitions(self.image(offset,size));self.assertEqual(table[1]['offset'],offset)
    def test_truncated_backup_and_invalid_layout(self):
        with self.assertRaises(ValueError):migration.partitions(b'')
        data=self.image();data[0x8004:0x8008]=struct.pack('<I',0xa000)
        with self.assertRaises(ValueError):migration.partitions(data)
    def test_partition_overflow(self):
        with self.assertRaises(ValueError):migration.partitions(self.image(0x390000,0x80000))
    def test_filesystem_migration_preserves_certificate_and_quarantines_overlay(self):
        import tempfile, subprocess, json
        tool=pathlib.Path.home()/'.platformio/packages/tool-mklittlefs/mklittlefs'
        if not tool.exists():self.skipTest('Install PlatformIO mklittlefs first')
        with tempfile.TemporaryDirectory() as temp:
            temp=pathlib.Path(temp);tree=temp/'tree';tree.mkdir()
            (tree/'wifi_ca.pem').write_text('test certificate bytes')
            (tree/'config.json').write_text('{"wifi_ssid":"stale-overlay"}')
            (tree/'future-setting.txt').write_text('preserve unknown file')
            old=temp/'old.bin'
            subprocess.run([str(tool),'-c',str(tree),'-s',str(0x1f0000),'-b','4096','-p','256',str(old)],check=True,capture_output=True)
            image=self.image();image[0x210000:]=old.read_bytes();image[0x9000:0x900b]=b'nvs-keep-me'
            backup=temp/'backup.bin';backup.write_bytes(image)
            target=migration.prepare(backup,str(tool),temp/'output')
            self.assertEqual(backup.read_bytes(),image)
            unpacked=temp/'unpacked';unpacked.mkdir()
            subprocess.run([str(tool),'-u',str(unpacked),'-s',str(0x70000),'-b','4096','-p','256',str(target)],check=True,capture_output=True)
            self.assertEqual((unpacked/'wifi_ca.pem').read_text(),'test certificate bytes')
            self.assertEqual((unpacked/'future-setting.txt').read_text(),'preserve unknown file')
            self.assertFalse((unpacked/'config.json').exists())
            self.assertTrue((unpacked/'migration-config.json').exists())
            self.assertFalse(json.loads((temp/'output/migration.json').read_text())['nvs_written'])
