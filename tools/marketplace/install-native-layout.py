"""Install JSON-native marketplace controls into RMD-MP's stash layout.

Preserves every unrelated byte, including the game's repeated JSON field keys.
Backs up changed files and rolls back if installation fails. No game saves/DLLs.
"""
import argparse
import json
from pathlib import Path
import os
import tempfile


def patch(text, controls):
    # Scan only the root children array; nested children and duplicate fields
    # must stay intact. json.dumps of the whole layout would lose duplicate keys.
    root = json.loads(text)
    if root.get("type") != "BankPanel" or root.get("name") != "BankExpansionLayout":
        raise ValueError("Expected the RMD expansion stash layout.")
    start = text.index("[", text.index('"children"')) + 1
    decoder = json.JSONDecoder()
    position, children = start, []
    while True:
        while text[position].isspace() or text[position] == ",":
            position += 1
        if text[position] == "]":
            break
        value, end = decoder.raw_decode(text, position)
        children.append((value, position, end))
        position = end
    if not children:
        raise ValueError("Stash has no native controls.")
    newline = "\r\n" if "\r\n" in text else "\n"
    replacements, added = [], []
    for control in controls:
        found = [(value, a, b) for value, a, b in children if value.get("name") == control["name"]]
        if len(found) > 1:
            raise ValueError("Duplicate marketplace control in stash layout.")
        raw = ("\n".join("        " + line for line in json.dumps(control, indent=4).splitlines())).replace("\n", newline)
        if found:
            value, a, b = found[0]
            if value != control:
                replacements.append((a, b, raw.lstrip()))
        else:
            added.append(raw)
    if added:
        end = children[-1][2]
        replacements.append((end, end, "," + newline + ("," + newline).join(added)))
    for a, b, raw in sorted(replacements, reverse=True):
        text = text[:a] + raw + text[b:]
    result = json.loads(text)
    for control in controls:
        assert [row for row in result["children"] if row["name"] == control["name"]] == [control]
    return text


def install(data, source=None, backup_directory=None):
    data = data.resolve()
    source = Path(source) if source is not None else Path(__file__).parent / "layouts"
    controls = [json.loads((source / name).read_text(encoding="utf-8")) for name in ("stash-shop-button.json", "marketplace-workspace.json", "stash-sync-mask.json")]
    targets = [data / "global/ui/layouts/bankexpansionlayouthd.json", data / "Pandemonium/UI Theme/remodded/layouts/bankexpansionlayouthd.json"]
    if not targets[0].is_file():
        raise ValueError("The active RMD-MP stash layout is missing.")
    changes = []
    for path in targets:
        if not path.exists():
            continue
        before = path.read_bytes()
        after = patch(before.decode("utf-8-sig"), controls).encode("utf-8")
        if before.startswith(b"\xef\xbb\xbf"):
            after = b"\xef\xbb\xbf" + after
        if before != after:
            changes.append((path, before, after))
    # Runtime packages are flat; source checkouts keep artwork beside layouts.
    for name in ('MarketplaceButton.sprite', 'MarketplaceButton.lowend.sprite', 'MarketplaceGift.sprite', 'MarketplaceGift.lowend.sprite'):
        asset = source / name
        if not asset.is_file(): asset = source.parent / 'assets' / name
        after = asset.read_bytes()
        if after[:4] != b'SpA1': raise ValueError('Invalid native marketplace button artwork.')
        path = data / 'Pandemonium' / 'Marketplace' / name
        before = path.read_bytes() if path.is_file() else None
        if before != after: changes.append((path, before, after))
    if not changes:
        return {"changed": 0, "message": "Native marketplace already installed."}
    backup = Path(backup_directory) if backup_directory is not None else Path(tempfile.mkdtemp(prefix="d2rhud-native-marketplace-"))
    backup.mkdir(parents=True, exist_ok=True)
    done = []
    try:
        for path, before, after in changes:
            relative = path.relative_to(data)
            target = backup / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            # Python 3.14 copy2 calls CopyFile2, unimplemented in older Wine.
            # These are byte-preserving layout backups, not save-file copies.
            if before is not None: target.write_bytes(before)
            if (path.read_bytes() if path.is_file() else None) != before:
                raise ValueError("Stash layout changed during installation.")
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as file:
                    temporary = Path(file.name)
                    file.write(after)
                    file.flush()
                    os.fsync(file.fileno())
                os.replace(temporary, path)
                done.append(path)
            finally:
                if temporary and temporary.exists():
                    temporary.unlink()
    except BaseException:
        previous = {path: before for path, before, _ in changes}
        for path in reversed(done):
            if previous[path] is None: path.unlink()
            else: path.write_bytes((backup / path.relative_to(data)).read_bytes())
        raise
    report = {"changed": len(done), "backup": str(backup), "files": [str(p) for p in done]}
    (backup / "installation.json").write_text(json.dumps(report, indent=2))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data", type=Path)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--backup", type=Path)
    args = parser.parse_args()
    print(json.dumps(install(args.data, args.source, args.backup), indent=2))
