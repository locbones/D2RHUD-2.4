"""Verify/regenerate the audited D3 Whirlwind native compatibility windows.

Usage:
  python tests/generate-d3-whirlwind-native.py --check-compatible D2R.exe
  python tests/generate-d3-whirlwind-native.py D2R.exe

The default updates only existing signature hashes in the feature's native and
pickup headers and implementation. --check-compatible never writes files.
Requires pefile. No game executable code is copied into the repository.
"""
from pathlib import Path
import argparse
import hashlib
import re
import struct

import pefile

EXPECTED = {
    "2c2ddb60a40ede3aaff51afb64e932621a03e32487687b6c64d88205fa652360",
    "e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8",
}
ROOT = Path(__file__).resolve().parents[1]
SOURCES = [ROOT / "d2rhud/plugin/D3WhirlwindNative.h",
           ROOT / "d2rhud/plugin/D3WhirlwindPickup.h",
           ROOT / "d2rhud/plugin/D3WhirlwindPotion.h",
           ROOT / "d2rhud/plugin/D3Whirlwind.cpp"]
ARRAY = re.compile(r"\b(\w*Signatures)\s*\{\{(.*?)\}\};", re.S)
ENTRY = re.compile(r"\{\s*(0x[0-9a-fA-F]+|[A-Za-z_]\w*)\s*,\s*0x([0-9a-fA-F]+)ULL\s*\}")


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-compatible", action="store_true")
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    source = args.executable.read_bytes()
    if hashlib.sha256(source).hexdigest() not in EXPECTED:
        raise SystemExit("Unsupported executable SHA-256: native behavior requires a new audit")
    pe = pefile.PE(data=source)
    if pe.FILE_HEADER.Machine != 0x8664 or pe.OPTIONAL_HEADER.ImageBase != 0x140000000:
        raise SystemExit("Unexpected executable architecture or image base")
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    # Independently verify the native table indexing used to identify the four
    # skill callbacks. This prevents entry hashes being reassigned to wrong slots.
    tables = [(0x14bf220, 38, 0x3741d0), (0x14bf500, 76, 0x374ca0),
              (0x14b9a00, 31, 0x17bf80), (0x14b9c30, 45, 0x17c310)]
    for table, index, expected in tables:
        if struct.unpack_from("<Q", image, table + index * 8)[0] != base + expected:
            raise SystemExit(f"Unexpected native skill table shape at {table:#x}[{index}]")
    # Type 7 (Whirlwind) uses the route builder whose cache must be invalidated
    # before retargeting, rather than only after native path computation.
    if struct.unpack_from("<Q", image, 0x1583bf0 + 7 * 8)[0] != base + 0x22cec0:
        raise SystemExit("Unexpected native Whirlwind path builder")

    total = 0
    seen = set()
    failures = []
    for path in SOURCES:
        content = path.read_text(encoding="utf-8")
        aliases = {name: int(value, 16) for name, value in
                   re.findall(r"\b([A-Za-z_]\w*)\s*=\s*(0x[0-9a-fA-F]+)\s*;", content)}

        def update_array(match):
            nonlocal total
            name, body = match.group(1), match.group(2)
            seen.add((path.name, name))

            def update_entry(entry):
                nonlocal total
                token, previous = entry.groups()
                if token.startswith("0x"):
                    address = int(token, 16)
                elif token in aliases:
                    address = aliases[token]
                else:
                    raise SystemExit(f"Unresolved signature RVA {token} in {path}")
                if address < 0 or address + 32 > len(image):
                    raise SystemExit(f"Signature outside executable: {address:#x}")
                actual = fnv(image[address:address + 32])
                total += 1
                if actual != int(previous, 16):
                    failures.append(f"{path.name}:{name} {address:#x}")
                return f"{{{token}, 0x{actual:016x}ULL}}"

            updated = ENTRY.sub(update_entry, body)
            return match.group(0).replace(body, updated, 1)

        updated = ARRAY.sub(update_array, content)
        if not args.check_compatible and updated != content:
            path.write_text(updated, encoding="utf-8")
    required = {(SOURCES[0].name, "Signatures"), (SOURCES[0].name, "InputSignatures"),
                (SOURCES[1].name, "Signatures"), (SOURCES[2].name, "Signatures")}
    if not required.issubset(seen):
        raise SystemExit("Missing native/input/pickup/potion signature arrays")
    if args.check_compatible and failures:
        raise SystemExit("Compatibility signature mismatch: " + ", ".join(failures))
    print(f"Verified executable SHA-256 and four native skill table slots; "
          f"{'checked' if args.check_compatible else 'regenerated'} {total} native/input/pickup/potion 32-byte FNV signatures.")


if __name__ == "__main__":
    main()
