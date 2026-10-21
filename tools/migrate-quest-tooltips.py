"""Redirect old quest/proof tooltips to the character journal, preserving source lessons."""
import argparse
import datetime
import json
from pathlib import Path
import re
import shutil


def migrate(path):
    original = path.read_text(encoding="utf-8-sig")
    count = 0
    marker = "D2RHUD legacy source (kept for journal export):"

    def block(match):
        nonlocal count
        text = match[0]
        if marker in text:
            return text
        proof = "Parchment of Proof" in match[1]
        guide = ("{grey}Legacy proof item: no longer required for quest progress.\n"
                 "{gold}Open Pandemonium > Quest Journal for your character's objectives and rewards.\n" if proof else
                 "{white}Quest progress is saved on your character.\n"
                 "{gold}Open Pandemonium > Quest Journal for objectives, progress and rewards.\n")

        def field(value):
            nonlocal count
            count += 1
            indent, key, literal = value.groups()
            return f"{indent}-- {marker} {key} = {literal}\n{indent}{key} = {json.dumps(guide)}"

        return re.sub(r'(?m)^([ \t]*)(suffix_desc|prefix_desc)\s*=\s*("(?:\\.|[^"\\])*")', field, text, count=1)

    updated = re.sub(r'(\{\s*-- (?:Evil Barrier \d+|Parchment of Proof[^\n]*)\n)[\s\S]*?(?=\{\s*--|\Z)', block, original)
    if updated == original:
        print(f"Already migrated: {path}")
        return
    if count != 55:
        raise ValueError(f"Expected 45 quest descriptions and 10 proof descriptions; found {count}")
    backup = path.with_name(path.name + ".before-quest-journal-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".bak")
    shutil.copy2(path, backup)
    with path.open("w", encoding="utf-8", newline="\r\n") as output:
        output.write(updated)
    print(f"Updated {count} descriptions; backup: {backup}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    for path in parser.parse_args().paths:
        migrate(path)
