#!/usr/bin/env python3
"""Build checked-in local translation assets. No network or credentials required."""
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'web/i18n'
SURFACES = {
    'marketing': ROOT / 'docs/js',
    'device': ROOT / 'data/www',
    'fleet': ROOT / 'fleet/sugarfleet/static',
}

def key(text):
    value = 2166136261
    raw = text.encode('utf-16-le')
    for index in range(0, len(raw), 2):
        value = ((value ^ int.from_bytes(raw[index:index+2], 'little')) * 16777619) & 0xffffffff
    digits = '0123456789abcdefghijklmnopqrstuvwxyz'
    result = ''
    while value:
        value, digit = divmod(value, 36)
        result = digits[digit] + result
    return result or '0'

def build():
    sources = json.loads((SOURCE / 'sources.json').read_text())
    all_keys = set(sum(sources.values(), []))
    if len({key(value) for value in all_keys}) != len(all_keys):
        raise ValueError('Translation key collision')
    for surface, destination in SURFACES.items():
        for name in ('i18n.js', 'i18n.css'):
            shutil.copyfile(SOURCE / name, destination / name)
        # Device catalogs are flash assets, not a duplicate LittleFS overlay.
        output = SOURCE / 'device-locales' if surface == 'device' else destination / 'locales'
        output.mkdir(exist_ok=True)
        for path in sorted((SOURCE / 'locales').glob('*.json')):
            translations = json.loads(path.read_text())
            missing = set(sources[surface]) - translations.keys()
            if missing:
                raise ValueError(f'{path.name}: missing {len(missing)} translations')
            compiled = {key(source): translations[source] for source in sources[surface]}
            (output / path.name).write_text(json.dumps(compiled, ensure_ascii=False, separators=(',', ':'))+'\n')
    bundled = ROOT / 'onboarding/TC001Setup/TC001Setup/Resources/WebUI/www'
    for path in (ROOT / 'data/www').iterdir():
        if path.is_file() and not path.name.startswith('.'):
            shutil.copyfile(path, bundled / path.name)

if __name__ == '__main__':
    build()
