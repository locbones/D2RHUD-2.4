"""Measure text in captured native presentation states using the installed font.

This supplements C++ hit-box checks: native TextBoxWidget can draw past its rect.
"""
import json
import re
import sys
from pathlib import Path
from PIL import ImageFont

layout = json.loads(Path('tools/marketplace/layouts/marketplace-workspace.json').read_text(encoding='utf-8'))
font_file = Path(sys.argv[1]) / 'hd/ui/fonts/exocetblizzardot-medium.otf'
failures = []
checked = 0


def verify(path):
    global checked
    model = json.loads(path.read_text(encoding='utf-8'))
    visible = {w['name']: w['visible'] for w in model['widgets']}
    text = {w['name']: w['text'] for w in model['widgets'] if w['text']}
    text.update({w['name']: w['label'] for w in model['previews']})

    def visit(node, shown=True):
        global checked
        name, fields = node['name'], node.get('fields', {})
        shown = shown and visible.get(name, fields.get('visible', True))
        if name == 'MarketplaceWorkspace':
            shown = True
        if not shown:
            return
        if node['type'] in ('TextBoxWidget', 'TextBoxWithLinksWidget'):
            value = text.get(name, fields.get('text', ''))
            if isinstance(value, str) and value.strip():
                style = fields.get('text/style', {}) if node['type'] == 'TextBoxWithLinksWidget' else fields.get('style', {})
                size = style.get('pointSize', 22)
                size = {'$SmallPanelFontSize': 22, '$MediumFontSize': 26}.get(size, size)
                size = size if isinstance(size, (int, float)) else 22
                font = ImageFont.truetype(str(font_file), int(size))
                width, height = fields['rect']['width'], fields['rect']['height']
                lines = []
                for paragraph in re.sub(r'ÿc.', '', value).split('\n'):
                    line = ''
                    for word in paragraph.split(' '):
                        candidate = (line + ' ' + word).strip()
                        if line and font.getlength(candidate) > width:
                            lines.append(line)
                            line = word
                        else:
                            line = candidate
                    lines.append(line)
                if any(font.getlength(line) > width + 1 for line in lines) or len(lines) * (size + 4) > height + 1:
                    failures.append(f'{path.stem}: {name}: {len(lines)} lines need {len(lines)*(size+4)}px in {width}x{height}: {value[:90]!r}')
                checked += 1
        for child in node.get('children', []):
            visit(child, shown)
    visit(layout)


for path in Path(sys.argv[2]).glob('*.json'):
    verify(path)
if failures:
    raise SystemExit('\n'.join(failures))
print(f'Native text: {checked} visible captions and summaries fit their boxes with the installed Exocet font.')
