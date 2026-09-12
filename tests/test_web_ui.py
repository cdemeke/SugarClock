from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / 'data/www'


class Elements(HTMLParser):
    def __init__(self, source):
        super().__init__()
        self.elements = []
        self.feed(source)

    def handle_starttag(self, tag, attrs):
        self.elements.append((tag, dict(attrs)))


class ConfigurationUITests(unittest.TestCase):
    def test_page_ids_and_script_targets(self):
        for page in WEB.glob('*.html'):
            with self.subTest(page=page.name):
                source = page.read_text()
                ids = [attrs['id'] for _, attrs in Elements(source).elements if 'id' in attrs]
                self.assertEqual([], [key for key, count in Counter(ids).items() if count > 1])
                targets = set(re.findall(r"getElementById\(['\"]([^'\"]+)['\"]\)", source))
                self.assertEqual(set(), targets - set(ids))

    def test_preview_scales_independently_of_canvas_id(self):
        css = (WEB / 'style.css').read_text()
        rule = re.search(r'\.ambient-matrix canvas\s*\{([^}]+)\}', css).group(1)
        for declaration in ('width: 100%', 'height: auto', 'aspect-ratio: 4 / 1', 'image-rendering: pixelated'):
            self.assertIn(declaration, rule)
        matrix = re.search(r'\.ambient-matrix\s*\{([^}]+)\}', css).group(1)
        self.assertGreaterEqual(int(re.search(r'max-width:\s*(\d+)px', matrix).group(1)), 784)
        canvas = [attrs for tag, attrs in Elements((WEB / 'index.html').read_text()).elements
                  if tag == 'canvas' and attrs.get('id') == 'ambient-companion-preview']
        self.assertEqual(1, len(canvas))
        self.assertEqual(4, int(canvas[0]['width']) / int(canvas[0]['height']))

    def test_installer_web_assets_match_device(self):
        bundled = ROOT / 'onboarding/TC001Setup/TC001Setup/Resources/WebUI/www'
        for asset in WEB.iterdir():
            if asset.is_file() and not asset.name.startswith('.'):
                with self.subTest(asset=asset.name):
                    self.assertEqual(asset.read_bytes(), (bundled / asset.name).read_bytes())
