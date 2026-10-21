"""Generate the scoped Act VI presentation patch contracts; does not patch a PE."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import CS_OP_MEM, X86_REG_RIP

root=Path(__file__).resolve().parents[1]
path=Path(sys.argv[1]);raw=path.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8'
pe=pefile.PE(data=raw);image=pe.get_memory_mapped_image()
md=Cs(CS_ARCH_X86,CS_MODE_64);md.detail=True

# Explicit sites, with operand byte offsets/widths established by decoding.
# Unrelated immediate 5/41/42 values must remain unchanged.
sites=[
    (0x8733e,48,'quest status initialization bound'),(0x8735b,48,'quest status initialization loop'),
    (0x147ac9,48,'quest description record lookup'),(0x148ff2,47,'selected quest status clamp'),
    (0xbb706e,48,'quest button decoration loop'),(0xbb721e,48,'quest status reset guard'),
    (0xbb7243,48,'quest status reset loop'),(0xbb7558,6,'quest tab visibility loop'),
    (0xbb79fa,48,'selected act quest button loop'),(0xbb7d65,48,'quest click record lookup'),
    (0xbb8458,48,'quest focus scan'),(0xbb84be,48,'quest selection scan'),
    (0xbb83ea,5,'last selected quest per-act lookup'),
    (0xc27b91,48,'quest notification record lookup'),(0xc28398,48,'quest objective text record lookup'),
    (0xc28708,48,'quest notification title lookup'),(0xc28802,48,'quest notification description lookup'),
    (0x243b31,0x22,'normalize 48 records in existing quest buffer'),
    (0x2bdbb2,48,'quest seen packet record validation'),
    (0xbb6ae3,0xa08,'quest panel sized deallocation'),(0xbb6c33,0xa08,'quest panel allocation'),
    (0xbb6c74,0x89c,'quest panel initialization'),
    (0xbb7249,0x2a0,'quest scratch initial clear'),(0xbb7597,0x2a0,'quest scratch tab clear'),
    (0xbb7703,0x2a2,'quest description text in moved scratch'),
    (0xbb775f,0x2a0,'quest description record in moved scratch'),
    (0xbb7e8e,0x3d1,'quest speech record in moved scratch'),
    (0xbb8137,0x3d1,'quest speech availability in moved scratch'),
    (0xbb82a1,0x3d8,'quest seen record in moved scratch'),
    (0xbb73b8,6,'quest tab unlock lookup bound'),
    (0x341b1,6,'preset-file cache constructor count'),
    (0x147882a,6,'preset-file cache destructor count'),
]
patches=[]
fmt=lambda b:'{'+','.join(f'0x{v:02x}' for v in b)+'}'
for rva,value,purpose in sites:
    ins=next(md.disasm(image[rva:rva+15],rva));before=bytes(ins.bytes);after=bytearray(before)
    displacement=rva in (0x243b31,0xbb7249,0xbb7597,0xbb7703,0xbb7e8e,0xbb8137,0xbb82a1)
    offset,size=(ins.disp_offset,ins.disp_size) if displacement else (ins.imm_offset,ins.imm_size)
    assert offset and size and len(before)<=12
    after[offset:offset+size]=value.to_bytes(size,'little')
    patches.append(f'    {{0x{rva:x},{len(before)},{fmt(before)},{fmt(after)}}}, // {purpose}')

# These four server stores award the final difficulty step during Baal kill,
# quest reward, NPC activation, and quest-state reconstruction respectively.
# Preserve Baal's quest bits, portal and movie; only defer that final step.
for rva in (0x2a3d32,0x33be69,0x33c20d,0x33cc8e):
    ins=next(md.disasm(image[rva:rva+15],rva));before=bytes(ins.bytes)
    assert ins.mnemonic=='mov' and 'word ptr [' in ins.op_str and '+ 0xa]' in ins.op_str
    patches.append(f'    {{0x{rva:x},{len(before)},{fmt(before)},{fmt(bytes([0x90])*len(before))}}}, // Act VI owns final difficulty step')
# The expansion ending also mirrors that award into the local client's title
# word. Keep the movie/ending notification while deferring its progress store.
ins=next(md.disasm(image[0x148a9f:0x148aae],0x148a9f));before=bytes(ins.bytes)
assert ins.mnemonic=='mov' and ins.op_str.startswith('word ptr [rip + ')
patches.append(f'    {{0x148a9f,{len(before)},{fmt(before)},{fmt(bytes([0x90])*len(before))}}}, // Defer local expansion completion title')

refs=[]
town_tests=[]
town_ranges=[]
for e in pe.DIRECTORY_ENTRY_EXCEPTION:
    a,b=e.struct.BeginAddress,e.struct.EndAddress
    # Recognize the complete five-town predicate, never an isolated 5 or 109.
    # All four early branches must lead to the same town path. The final
    # comparison receives level-103, so Refuge (240) arrives as 137.
    instructions=list(md.disasm(image[a:b],a))
    for i in range(len(instructions)-9):
        seq=instructions[i:i+10]
        reg=seq[0].op_str.split(', ')[0]
        if not (all(seq[k].mnemonic=='sub' and seq[k].op_str==reg+', '+value
                    for k,value in [(0,'1'),(2,'0x27'),(4,'0x23'),(6,'0x1c')])
                and all(seq[k].mnemonic=='je' for k in [1,3,5,7])
                and len({seq[k].op_str for k in [1,3,5,7]})==1
                and seq[8].mnemonic=='cmp' and seq[8].op_str==reg+', 6'
                and seq[9].mnemonic in ('je','jne')):continue
        cmp,jump=seq[8:];before=image[cmp.address:jump.address+jump.size]
        assert 5<=len(before)<=10 and reg in ['eax','ecx','edx','r8d','r9d']
        number={'eax':0,'ecx':1,'edx':2,'r8d':8,'r9d':9}[reg]
        town=int(seq[7].op_str,16)
        yes=int(jump.op_str,16) if jump.mnemonic=='je' else jump.address+jump.size
        no=jump.address+jump.size if jump.mnemonic=='je' else int(jump.op_str,16)
        # A few scripts dispatch individual towns to different handlers;
        # those are not a shared town predicate and need their own design.
        if yes!=town:continue
        town_tests.append(f'    {{0x{cmp.address:x},{len(before)},{number},0x{seq[0].address:x},0x{yes:x},0x{no:x},{fmt(before)}}},')
        town_ranges.append((seq[0].address,jump.address+jump.size-seq[0].address))
    for ins in instructions:
        for operand in ins.operands:
            if operand.type!=CS_OP_MEM:continue
            rip=operand.mem.base==X86_REG_RIP
            target=ins.address+ins.size+operand.mem.disp if rip else operand.mem.disp
            if 0x14b9200<=target<=0x14b95e8:
                offset=target-0x14b9200
                if offset>=41*24:offset+=7*24 # End iterator points at script field.
                kind=0
            elif 0x184ec28<=target<0x184ec3c:offset=target-0x184ec28;kind=1
            elif 0x1ca0b30<=target<0x1ca0ba8:offset=target-0x1ca0b30;kind=2
            elif ins.address==0x147882f:
                assert target==0x1ca0ba8
                offset=6*24;kind=2 # Reverse destructor's end iterator.
            elif ins.address==0x25ba0f:
                assert target==0x1854720
                offset=0;kind=3 # Six independent preset path string_views.
            else:continue
            assert ins.disp_size==4 and len(ins.bytes)<=12
            refs.append(f'    {{0x{ins.address:x},{len(ins.bytes)},{ins.disp_offset},{str(rip).lower()},{kind},0x{offset:x},{fmt(ins.bytes)}}},')

def fnv(data):
    value=14695981039346656037
    for b in data:value=((value^b)*1099511628211)&0xffffffffffffffff
    return value

# Full execution paths used by the prototype, and contexts of individual edits.
ranges=[(r-16,48) for r,_,_ in sites]+[
    (0x2a3cc0,0x77),(0x33bd30,0x16f),(0x33c0e0,0x1d0),(0x33cb20,0x1a4),
    (0x148a76,0x58),
    (0xc32320,0x240),(0xc32560,0xc),(0x2e16d0,0x40),
    (0xb9a720,0x93),
    (0x26e880,0x54), # tile creation entry and exit-marker dispatch
    (0x341a0,0x40),(0x1478820,0x3d),(0x25b8f0,0x2d),
    (0x25b9e9,0x3b),(0x2606a8,0x38),(0x1d0a30,0x2c),
    (0x147a70,0x6d0),(0x1479b0,0xb6),(0x2591d0,0xff),(0x13f580,0x67),
    (0xbb73a1,8),(0x1531a0,0x11d),(0x152800,0x33d),
    (0x318d9f,0x64),(0x31d5fd,0x125),(0x2591dc,0x2c),(0x259259,0x2f),
]+town_ranges
assert len(town_tests)>100
out='''// Generated by tests/generate-act6-native.py. Not a global bound replacement.
#pragma once
#include <array>
#include <cstdint>
namespace ExtendedActs::Contracts {
struct Patch {uint32_t rva;uint8_t size;std::array<uint8_t,12> before,after;};
inline constexpr std::array<Patch,%d> Patches{{
%s
}};
struct Relocation {uint32_t rva;uint8_t size,displacement;bool rip;uint8_t table;uint32_t offset;std::array<uint8_t,12> before;};
inline constexpr std::array<Relocation,%d> Relocations{{
%s
}};
struct TownTest {uint32_t rva;uint8_t size,reg;uint32_t start,yes,no;std::array<uint8_t,10> before;};
inline constexpr std::array<TownTest,%d> TownTests{{
%s
}};
struct Signature {uint32_t rva,size;uint64_t hash;};
inline constexpr std::array<Signature,%d> Signatures{{
%s
}};
}
'''%(len(patches),'\n'.join(patches),len(refs),'\n'.join(refs),len(town_tests),'\n'.join(town_tests),len(ranges),
       '\n'.join(f'    {{0x{r:x},0x{n:x},0x{fnv(image[r:r+n]):016x}ULL}},' for r,n in ranges))
(root/'d2rhud/plugin/ExtendedActsContracts.h').write_text(out,encoding='utf-8')
print(f'Generated {len(patches)} scoped changes, {len(refs)} native table references and {len(town_tests)} complete town predicates.')
