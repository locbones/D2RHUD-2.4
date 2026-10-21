"""Write a separate skills.txt with Paladin auras enabled in both slots.

Ported from the colleague's D2RHUD-combined-patch-source.zip. The HUD enables
eligible aura flags in memory; this optional utility makes a mod table explicit.
"""
from pathlib import Path
import argparse
import json


def enable_left_auras(raw: bytes) -> tuple[bytes, list[str]]:
    lines = raw.splitlines(keepends=True)
    if not lines:
        raise ValueError("The skills table is empty")
    header = lines[0].decode("utf-8-sig").rstrip("\r\n").split("\t")
    indices = {key: header.index(key) for key in ("skill", "charclass", "aura", "leftskill", "rightskill")}
    changed = []
    for number, line in enumerate(lines[1:], 1):
        body = line.rstrip(b"\r\n")
        cells = body.split(b"\t")
        if len(cells) <= max(indices.values()):
            continue
        if cells[indices["charclass"]] != b"pal" or cells[indices["aura"]] != b"1":
            continue
        if cells[indices["rightskill"]] != b"1":
            raise ValueError(f"Paladin aura on line {number + 1} is not enabled for the right slot")
        if cells[indices["leftskill"]] != b"1":
            changed.append(cells[indices["skill"]].decode("utf-8"))
            cells[indices["leftskill"]] = b"1"
            lines[number] = b"\t".join(cells) + line[len(body):]
    return b"".join(lines), changed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skills", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if args.skills.resolve() == args.out.resolve():
        parser.error("Use a separate output path; the input table is never overwritten")
    if args.out.exists():
        parser.error("Output already exists; choose a new output path")
    try:
        output, changed = enable_left_auras(args.skills.read_bytes())
    except (OSError, ValueError) as error:
        parser.error(str(error))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation also prevents overwriting a file created since preflight.
    with args.out.open("xb") as stream:
        stream.write(output)
    print(json.dumps({"out": str(args.out.resolve()), "changed": changed, "rightskill_preserved": True}, indent=2))


if __name__ == "__main__":
    main()
