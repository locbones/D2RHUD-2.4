"""Export RMD-MP's Enhancement Crystal descriptions without executing Lua."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def reference(text, catalog):
    rows = []
    blocks = list(re.finditer(r'\{\s*--Enhancement Crystal\s*\n', text))
    for match in blocks:
        end = text.find('{ --', match.end())
        block = text[match.end():end if end >= 0 else len(text)]
        code = re.search(r'\bcode\s*=\s*"([A-Za-z0-9]{3})"', block)
        english = re.search(r'\benUS\s*=\s*("(?:\\.|[^"\\])*")', block)
        if not code or not english:
            raise ValueError('Incomplete Enhancement Crystal description')
        code = code[1]
        base = catalog['bases'][code]
        original = base.get('original_name', base['name'])
        if not original.startswith('Enhancement Crystal ('):
            raise ValueError('Unexpected crystal base name: ' + original)
        description = []
        kind = 'magic'
        for line in json.loads(english[1]).replace('%%', '%').splitlines():
            for token in re.findall(r'\{([^}]+)\}', line):
                if token not in ('blue', 'purple'):
                    raise ValueError('Unsupported crystal description token: ' + token)
                kind = 'recipe-note' if token == 'purple' else 'magic'
            clean = re.sub(r'\{[^}]+\}', '', line).strip()
            if clean:
                description.append({'text': clean, 'kind': kind})
        if len(description) < 5 or not any(r['kind'] == 'recipe-note' for r in description):
            raise ValueError('Missing crystal or Blacksmith effects: ' + code)
        effect = re.sub(r'^[+-]?\d+(?:-\d+)?%?\s*(?:to\s+)?', '', description[1]['text'])
        uses = original.removeprefix('Enhancement Crystal ')
        name = 'Enhancement Crystal - ' + effect + ' ' + uses
        rows.append({'code': code, 'name': name, 'description': description})
    expected = {f'Z{n}' for n in range(60, 100)} | {f'z{n:02}' for n in range(28)}
    if len(rows) != 68 or {r['code'] for r in rows} != expected:
        raise ValueError('Expected all 68 distinct Enhancement Crystal codes')
    if len({r['name'] for r in rows}) != len(rows):
        raise ValueError('Crystal effect names must distinguish every variant')
    return {'schema': 1, 'mod': 'RMD-MP',
            'source': 'Pandemonium/Filters/override_rules.lua',
            'source_sha256': hashlib.sha256(text.encode('utf-8')).hexdigest(), 'items': rows}


def apply(catalog, data):
    for row in data['items']:
        base = catalog['bases'][row['code']]
        base.setdefault('original_name', base['name'])
        base['name'] = row['name']
        base['description'] = row['description']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rules', type=Path)
    parser.add_argument('--apply-catalog', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    path = root / 'tools/marketplace/server/public/assets/catalog.json'
    catalog = json.loads(path.read_text(encoding='utf-8'))
    data = reference(args.rules.read_text(encoding='utf-8-sig'), catalog)
    (root / 'tools/marketplace/assets/rmd-crystals.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    if args.apply_catalog:
        apply(catalog, data)
        path.write_text(json.dumps(catalog, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    print('Exported 68 Enhancement Crystal effects, usage limits and Blacksmith descriptions.')


if __name__ == '__main__':
    main()
