"""Check the Lua export against the website's exact crystal identities."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('crystals', ROOT / 'tools/marketplace/build-crystal-reference.py')
crystals = importlib.util.module_from_spec(spec)
spec.loader.exec_module(crystals)


class CrystalReferences(unittest.TestCase):
    def test_all_effects_and_original_identities(self):
        data = json.loads((ROOT / 'tools/marketplace/assets/rmd-crystals.json').read_text(encoding='utf-8'))
        catalog = json.loads((ROOT / 'tools/marketplace/server/public/assets/catalog.json').read_text(encoding='utf-8'))
        self.assertEqual(len(data['items']), 68)
        for row in data['items']:
            base = catalog['bases'][row['code']]
            self.assertEqual(base['name'], row['name'])
            self.assertEqual(base['description'], row['description'])
            self.assertTrue(base['original_name'].startswith('Enhancement Crystal ('))
            self.assertEqual(base['code'], row['code'])
            self.assertIn('Blacksmiths', ' '.join(p['text'] for p in row['description']))
            self.assertNotIn('%%', json.dumps(row['description']))
            self.assertNotIn('{blue}', json.dumps(row['description']))
        before = json.dumps(catalog, sort_keys=True)
        crystals.apply(catalog, data)
        self.assertEqual(json.dumps(catalog, sort_keys=True), before)

    def test_incomplete_sources_refuse_to_replace_reference(self):
        with self.assertRaises(ValueError):
            crystals.reference('return {}', {'bases': {}})
        with self.assertRaises(ValueError):
            crystals.reference('{ --Enhancement Crystal\n code="Z60"\n}', {'bases': {}})


if __name__ == '__main__':
    unittest.main()
