#!/usr/bin/env python3
"""Verify launch-kit metadata, referenced files, exports and draft boundaries."""
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlsplit, unquote
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parent
metadata=json.loads((ROOT/'app-store-metadata.json').read_text())
for field,limit in [('name',30),('subtitle',30),('promotional_text',170),('description',4000)]:
    assert 0 < len(metadata[field]) <= limit, (field,len(metadata[field]))
assert len(metadata['keywords'].encode('utf-8'))<=100
assert 'REQUIRED:' in metadata['owner_required']['privacy_contact']

class Page(HTMLParser):
    def __init__(self,file):
        super().__init__();self.file=file;self.ids=set();self.links=[];self.noindex=False
    def handle_starttag(self,tag,attrs):
        attrs=dict(attrs)
        if 'id' in attrs:self.ids.add(attrs['id'])
        if tag=='img':assert attrs.get('alt') is not None, self.file
        if tag=='meta' and attrs.get('name')=='robots':self.noindex='noindex' in attrs.get('content','')
        for key in ('href','src'):
            if key in attrs:self.links.append(attrs[key])
for file in (ROOT/'site').glob('*.html'):
    page=Page(file);page.feed(file.read_text());assert page.noindex,file
    for url in page.links:
        target=urlsplit(url)
        if target.scheme or target.netloc:continue
        if target.path:assert (file.parent/unquote(target.path)).resolve().exists(),(file,url)
        elif target.fragment:assert target.fragment in page.ids,(file,url)

manifest=json.loads((ROOT/'asset-manifest.json').read_text())
assert len(manifest['files'])==16
for record in manifest['files']:
    content=(ROOT/record['file']).read_bytes()
    assert content[:8]==b'\x89PNG\r\n\x1a\n'
    width,height=struct.unpack('>II',content[16:24])
    assert (width,height)==(record['width'],record['height'])
    assert content[25]==2, ('alpha channel',record['file'])
    assert hashlib.sha256(content).hexdigest()==record['sha256']
assert (ROOT/'assets/app-icon-1024.png').read_bytes()==(ROOT.parent/'SugarClock/Assets.xcassets/AppIcon.appiconset/icon_appstore.png').read_bytes()
print('Launch kit verified: metadata bounds, page links/alt text/draft markers, 16 opaque image exports and hashes, icon provenance.')
