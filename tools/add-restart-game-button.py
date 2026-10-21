"""Add the native pause-menu button to an unpacked mod, including theme variants.

Usage: python tools/add-restart-game-button.py <mod.mpq folder> [...]
Only existing PauseLayoutHD.json files are edited. Originals are backed up.
"""
from pathlib import Path
import datetime
import re
import sys


def object_end(text, start):
    depth = 0
    quoted = escaped = False
    for i in range(start, len(text)):
        c = text[i]
        if quoted:
            if escaped:
                escaped = False
            elif c == '\\':
                escaped = True
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if not depth:
                return i + 1
    raise ValueError('Unclosed layout object')


def patch(text, controller=False):
    if 'PausePanelMessage:RestartGame' in text:
        # Upgrade the original unsupported custom action without inserting a
        # second row or moving any buttons a second time.
        return text.replace('PausePanelMessage:RestartGame', 'PanelManager:OpenPanel:D2RHUDRestartGame')
    if 'PanelManager:OpenPanel:D2RHUDRestartGame' in text:
        return text
    candidates = []
    for m in re.finditer(r'\{\s*"type"\s*:\s*"(ButtonWidget|TableRowWidget)"', text):
        end = object_end(text, m.start())
        if 'PausePanelMessage:ExitGame' in text[m.start():end]:
            candidates.append((m.start(), end))
    if not candidates:
        raise ValueError('No native ExitGame button found')
    start, end = min(candidates)  # include its table row when present
    template = text[start:end]
    button = re.sub(r'("name"\s*:\s*")Exit"', r'\1RestartGame"', template)
    button = button.replace('Row Save and Exit Game', 'Row Restart Game')
    button = button.replace('@SaveAndExit', 'Restart Game').replace('PausePanelMessage:ExitGame', 'PanelManager:OpenPanel:D2RHUDRestartGame')
    indent = re.search(r'[^\n]*$', text[:start]).group()
    if indent.strip():
        raise ValueError('Expected a layout object on its own line')
    if controller:
        # Keep the existing controller button style/position and move Exit down
        # one normal row. Both remain inside the 1440-unit reference layout.
        m = re.search(r'("rect"\s*:\s*\{[^}]*"y"\s*:\s*)(-?\d+)', template)
        if not m:
            raise ValueError('Controller Exit button has no position')
        template = template[:m.start(2)] + str(int(m.group(2)) + 150) + template[m.end(2):]
    result = text[:start] + button + ',\n' + indent + template + text[end:]
    if not controller:
        # The table gains one row: keep the group centered, preserving mod style.
        table = re.search(r'"name"\s*:\s*"PauseTable"', result)
        if table:
            m = re.search(r'("rect"\s*:\s*\{[^}]*"y"\s*:\s*)(-?\d+)', result[table.end():])
            height = re.search(r'"rowHeight"\s*:\s*(\d+)', result[table.end():])
            if m and height:
                a, b = table.end() + m.start(2), table.end() + m.end(2)
                result = result[:a] + str(int(m.group(2)) - int(height.group(1)) // 2) + result[b:]
    return result


def main():
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    for root in map(Path, sys.argv[1:]):
        if not root.is_dir():
            raise SystemExit(f'Mod folder not found: {root}')
        paths = sorted(p for p in (root / 'data').rglob('*.json') if p.name.lower() == 'pauselayouthd.json')
        for path in paths:
            text = path.read_text(encoding='utf-8-sig')
            result = patch(text, path.parent.name.lower() == 'controller')
            if result == text:
                continue
            backup = path.with_name(path.name + f'.before-restart-game-{stamp}.bak')
            backup.write_bytes(path.read_bytes())
            path.write_text(result, encoding='utf-8', newline='\n')
            print(path)


if __name__ == '__main__':
    main()
