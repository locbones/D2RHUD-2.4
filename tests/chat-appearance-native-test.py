"""Verify the chat scalar-field contract against the installed 2.4 executable."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import capstone
import pefile

path = Path(sys.argv[1])
pe = pefile.PE(str(path))
base = pe.OPTIONAL_HEADER.ImageBase
source = (Path(__file__).resolve().parents[1] / 'd2rhud/plugin/D2RHUD/ChatAppearance.cpp').read_text(encoding='utf-8')
for rva, expected in re.findall(r'Matches\((0x[0-9a-f]+), (0x[0-9a-f]+)ULL\)', source):
    value = 14695981039346656037
    for byte in pe.get_data(int(rva, 16), 32):
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    assert value == int(expected, 16), rva
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)


def instructions(rva, size):
    return list(md.disasm(pe.get_data(rva, size), base + rva))


assert struct.unpack('<Q', pe.get_data(0x15ab2f0 + 0x10, 8))[0] == base + 0x568e90
assert struct.unpack('<Q', pe.get_data(0x15ab2f0 + 0x18, 8))[0] == base + 0x5b2140
assert struct.unpack('<Q', pe.get_data(0x15ac8b0 + 0x18, 8))[0] == base + 0x5795f0
assert any(i.mnemonic == 'mov' and i.op_str == 'edx, dword ptr [rcx + 0x200]' for i in instructions(0x56a9b0, 20))
assert any(i.mnemonic == 'mulss' and '[r10 + 0x80]' in i.op_str for i in instructions(0x576dd0, 0x180))
assert any(i.mnemonic == 'movups' and '[rcx + 0x70]' in i.op_str for i in instructions(0x576dd0, 0x80))
assert any(i.mnemonic == 'mov' and i.op_str == 'qword ptr [rbx + 0x94], 0x3f800000' for i in instructions(0x579390, 0x90))
assert any(i.mnemonic == 'lea' and i.op_str == 'rax, [r15 + 0x88]' for i in instructions(0x5795f0, 0x80))
assert any(i.mnemonic == 'call' and i.op_str == hex(base + 0x155700) for i in instructions(0x57b700, 0x500))
print(json.dumps({'binary_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                  'entry_guards': 'passed', 'native_chat_types': 'passed',
                  'scale_including_native_text': 'passed', 'viewport_fields': 'passed',
                  'rectangle_rgba_and_alpha': 'passed', 'source': 'read-only PE/Capstone checks; no game writes'}, indent=2))
