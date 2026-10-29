"""Localization completeness, data-boundary, and packaging invariants."""
import html
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_i18n import key, SURFACES

class LocalizationTests(unittest.TestCase):
    def test_catalogs_cover_every_source_and_preserve_parameters_and_brands(self):
        source = ROOT / 'web/i18n'
        inventory = json.loads((source / 'sources.json').read_text())
        expected = set(sum(inventory.values(), []))
        files = list((source / 'locales').glob('*.json'))
        self.assertEqual({p.stem for p in files}, set('zh hi es ar fr bn pt ru ur id de ja mr vi te sw ha tr ta'.split()))
        self.assertEqual(len(expected), len({key(value) for value in expected}))
        for path in files:
            data = json.loads(path.read_text())
            self.assertFalse(expected - data.keys(), path.name)
            for original in expected:
                with self.subTest(locale=path.stem, source=original):
                    translated = data[original]
                    self.assertTrue(translated.strip())
                    self.assertEqual(sorted(re.findall(r'\{\d+\}', original)), sorted(re.findall(r'\{\d+\}', translated)))
                    for brand in ('SugarClock','Nightscout','Dexcom','LibreLinkUp','Ulanzi','TC001','GitHub'):
                        if brand in original:
                            self.assertIn(brand, translated)

    def test_static_copy_is_catalogued_and_assets_are_synchronized(self):
        inventory = json.loads((ROOT / 'web/i18n/sources.json').read_text())
        for surface, folder in SURFACES.items():
            pages = list((ROOT / {'marketing':'docs','device':'data/www','fleet':'fleet/sugarfleet/templates'}[surface]).glob('*.html'))
            for page in pages:
                for value in re.findall(r'data-i18n(?:-[\w-]+)?="([^"]+)"', page.read_text()):
                    self.assertIn(html.unescape(value), inventory[surface], str(page))
            for name in ('i18n.js', 'i18n.css'):
                self.assertEqual((ROOT / 'web/i18n' / name).read_bytes(), (folder / name).read_bytes())
            output = ROOT / 'docs/js/device-locales' if surface == 'device' else folder / 'locales'
            for catalog in (ROOT / 'web/i18n/locales').glob('*.json'):
                translations = json.loads(catalog.read_text())
                compiled = json.loads((output / catalog.name).read_text())
                self.assertEqual(compiled, {key(value):translations[value] for value in inventory[surface]})

    def test_device_catalogs_are_published_on_the_site_not_embedded_in_firmware(self):
        generated = (ROOT / 'src/web_assets.cpp').read_text()
        self.assertNotIn('"/locales/', generated)
        self.assertFalse((ROOT / 'data/www/locales').exists())
        self.assertEqual(len(list((ROOT / 'docs/js/device-locales').glob('*.json'))), 19)
        for page in (ROOT / 'data/www').glob('*.html'):
            if 'i18n.js' in page.read_text():
                self.assertIn('<script src="i18n.js" data-catalogs="https://sugarclock.com/js/device-locales/"></script>',
                              page.read_text(), page.name)
