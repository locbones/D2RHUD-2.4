"""Audit native player creation, skill actions and save isolation entry points.

Run: python tests/generate-survivors-native.py PATH_TO_D2R.exe [OUTPUT_HEADER]
Only hashes and function bounds are emitted; native code stays in the game.
"""
from pathlib import Path
import hashlib
import struct
import sys
import pefile

source = Path(sys.argv[1])
# Primary reference: the supplied Downloads/D2R.exe. Both audited files have
# identical Survivors code; their six differing bytes are allocation constants
# outside these entry windows. Runtime installation still checks every window.
REFERENCE_SHA256 = "E4E13570E60C0A65CB0469D6659BE8A6008F9433AC50DB1C4F94501D7D9016F8"
AUDITED_SHA256 = {
    REFERENCE_SHA256,
    "2C2DDB60A40EDE3AAFF51AFB64E932621A03E32487687B6C64D88205FA652360",
}
source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest().upper()
assert source_sha256 in AUDITED_SHA256, "Unsupported executable"
pe = pefile.PE(str(source))
data = pe.get_memory_mapped_image()
entries = {e.struct.BeginAddress: e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION}

def root(entry):
    header = data[entry.UnwindData:entry.UnwindData + 4]
    if header[0] >> 3 & 4:
        offset = 4 + ((header[2] + 1) & ~1) * 2
        return root(entries[struct.unpack_from('<I', data, entry.UnwindData + offset)[0]])
    return entry.BeginAddress

groups = {}
for entry in entries.values():
    groups.setdefault(root(entry), []).append(entry)
rows = []
def fnv64(chunk):
    digest = 14695981039346656037
    for byte in chunk:
        digest = ((digest ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return digest

for address in (0x2934b0,0x318c80,0x28f2b0,0x29f350,0x29f090,0x31a280,
                0x29d890,0x2c52e0,0x29d8f0,0x29de20,0x341670,0x2192d0,
                0x218ce0,0x22e8a0,0x1f73e0,0x2c7a90,0x2aaa00,
                0x29a2a0,0x2cce40,0x298550,0x298810,0x2095a0,0x2ce6c0,
                # 2d5e10 is already audited/detoured by CubeVisual, which is a
                # prerequisite. Do not reject that installed HUD detour here.
                0x28c740,0x28c520,0x6d01a0,0x2d5890,0x2f70a0,
                0x6ff60,0x70007,0x70065,0x6d0470,0x1d4e15,0x2cb670,0x320fa0,0x3208b0,0x320a10):
    # Entry windows avoid table-extension patches inside these larger functions.
    # Native ABI fixtures exercise the important bodies separately.
    end = address+32
    digest = fnv64(data[address:end])
    rows.append('    {0x%x,0x%x,0x%016xULL},' % (address, end-address, digest))

# D2RLAN/Pandemonium's "Customize Save File Limit" edit changes eight
# immediate bytes together: allocation, cookie/register slots, serializer
# capacity, saved-buffer limit, restored slots and stack release. It keeps the
# native ABI. Accept only the complete original or complete 32-KiB routine.
save_begin = 0x28f2b0
save_end = max(entry.EndAddress for entry in groups[save_begin])
assert save_end == 0x28f507
save_offsets = (0x28f2b7, 0x28f2d1, 0x28f2f9, 0x28f324,
                0x28f454, 0x28f47f, 0x28f487, 0x28f496)
original_save = data[save_begin:save_end]
expanded_save = bytearray(original_save)
for address in save_offsets:
    assert expanded_save[address-save_begin] == 0x20
    expanded_save[address-save_begin] = 0x80
for block in pe.DIRECTORY_ENTRY_BASERELOC:
    assert not any(entry.type and save_begin <= entry.rva < save_end for entry in block.entries)
save_metadata = (
    '// Full save-dispatch variants: original 8 KiB and the complete launcher 32-KiB edit.\n'
    'inline constexpr Signature SaveDispatchOriginal{0x%x,0x%x,0x%016xULL};\n'
    'inline constexpr Signature SaveDispatch32K{0x%x,0x%x,0x%016xULL};\n'
    % (save_begin, save_end-save_begin, fnv64(original_save),
       save_begin, save_end-save_begin, fnv64(expanded_save))
    + 'inline constexpr std::array<uint32_t,8> SaveLimitPatchSites{{'
    + ','.join('0x%x' % address for address in save_offsets) + '}};\n'
)
destination = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[1] / 'd2rhud/plugin/SurvivorsNative.h'
destination.write_text('// Audited D2R 1.2.69270 avatar, save and skill-action entry windows.\n'
    '// Reference executable SHA-256: %s\n' % source_sha256 +
    '#pragma once\n#include <array>\n#include <cstdint>\nnamespace CubeMap::SurvivorsNative {\nstruct Signature {uint32_t rva,size;uint64_t hash;};\ninline constexpr std::array<Signature,%d> Signatures{{\n' % len(rows) + '\n'.join(rows) + '\n}};\n'
    + save_metadata + '// Character persistence needs only these shared loader/writer hooks.\n'
    'inline constexpr std::array<Signature,2> SaveLoadSignatures{{Signatures[0],Signatures[1]}};\n'
    'static_assert(SaveLoadSignatures[0].rva==0x2934b0 && SaveLoadSignatures[1].rva==0x318c80);\n}\n', encoding='utf-8')
print('Generated', destination)
