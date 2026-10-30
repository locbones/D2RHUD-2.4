"""Generate relocation/unwind metadata for the audited D2R 2.4 cube ingredient matcher.
Only metadata is emitted; executable instructions are copied from the verified game.
"""
from pathlib import Path
import struct
import sys
import capstone
import pefile

source=Path(sys.argv[1]);raw=source.read_bytes();pe=pefile.PE(data=raw)
data=bytearray(pe.OPTIONAL_HEADER.SizeOfImage)
for section in pe.sections:
    data[section.VirtualAddress:section.VirtualAddress+section.SizeOfRawData]=raw[section.PointerToRawData:section.PointerToRawData+section.SizeOfRawData]
begin,end,code_end=0x305d00,0x30684c,0x306818
value=14695981039346656037
for byte in data[begin:end]:value=((value^byte)*1099511628211)&0xffffffffffffffff
assert value==0x0cd622517e6e82e9,'Unsupported cube ingredient matcher'
assert data[begin+0x46e:begin+0x473]==bytes.fromhex('410fb64704')
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
fixups=[]
for ins in md.disasm(data[begin:code_end],begin):
    assert 'xmm' not in ins.op_str,'Callback must preserve any newly live SIMD registers'
    if ins.disp_size==4 and any(op.type==capstone.CS_OP_MEM and op.mem.base==capstone.x86.X86_REG_RIP for op in ins.operands):
        fixups.append((ins.address-begin,ins.disp_offset,ins.size,ins.address+ins.size+ins.disp))
    if ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP):
        if ins.operands and ins.operands[0].type==capstone.CS_OP_IMM:
            target=ins.operands[0].imm
            if not begin<=target<end:
                assert ins.imm_size==4
                fixups.append((ins.address-begin,ins.imm_offset,ins.size,target))
entries=[e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION if begin<=e.struct.BeginAddress<end]
blocks={}
def unwind(rva):
    if rva in blocks:return
    n=4+((data[rva+2]+1)&~1)*2
    flags=data[rva]>>3;assert flags in (0,4)
    blocks[rva]=bytearray(data[rva:rva+n+(12 if flags&4 else 0)])
    if flags&4:unwind(struct.unpack_from('<III',data,rva+n)[2])
for e in entries:unwind(e.UnwindData)
locations={};at=0x1200
for rva,block in sorted(blocks.items()):locations[rva]=at;at+=(len(block)+3)&~3
for rva,block in blocks.items():
    if block[0]>>3&4:
        n=4+((block[2]+1)&~1)*2;a,b,c=struct.unpack_from('<III',block,n)
        assert begin<=a<b<=end
        struct.pack_into('<III',block,n,0x2000+a-begin,0x2000+b-begin,locations[c])
lines=['#pragma once','#include <array>','#include <cstdint>','namespace CubeVisual::ConditionNativeData {',
       'struct Fixup {uint16_t instruction;uint8_t displacement,length;uint32_t target;};','inline constexpr Fixup Fixups[]{']
lines += [f'    {{0x{a:x},{b},{c},0x{d:x}}},'for a,b,c,d in fixups]
lines += ['};','struct UnwindBlock {uint16_t offset,size;std::array<uint8_t,64> bytes;};','inline constexpr UnwindBlock UnwindBlocks[]{']
for rva,block in sorted(blocks.items()):
    assert len(block)<=64
    lines += [f'    {{0x{locations[rva]:x},{len(block)},{{'+','.join(f'0x{b:02x}'for b in block)+'}},']
lines += ['};','struct Runtime {uint32_t begin,end,unwind;};','inline constexpr Runtime RuntimeFunctions[]{']
lines += [f'    {{0x{0x2000+e.BeginAddress-begin:x},0x{0x2000+e.EndAddress-begin:x},0x{locations[e.UnwindData]:x}}},'for e in entries]
main=next(e for e in entries if e.BeginAddress<=begin+0x46e<e.EndAddress)
lines += ['};',f'inline constexpr Runtime CallbackParent{{0x{0x2000+main.BeginAddress-begin:x},0x{0x2000+main.EndAddress-begin:x},0x{locations[main.UnwindData]:x}}};','}']
Path('d2rhud/plugin/CubeConditionNativeData.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f'Generated {len(fixups)} relative operands and {len(entries)} chained unwind ranges')
