"""Generate audited per-invocation native cube-output relocation/unwind metadata."""
from pathlib import Path
import capstone,pefile,struct,sys
raw=Path(sys.argv[1]).read_bytes();pe=pefile.PE(data=raw);data=bytearray(pe.OPTIONAL_HEADER.SizeOfImage)
for s in pe.sections:data[s.VirtualAddress:s.VirtualAddress+s.SizeOfRawData]=raw[s.PointerToRawData:s.PointerToRawData+s.SizeOfRawData]
begin,end,code_end=0x306c50,0x30883c,0x308824
h=14695981039346656037
for b in data[begin:end]:h=((h^b)*1099511628211)&0xffffffffffffffff
assert h==0xac545ecdc36232fc
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
ins=list(md.disasm(data[begin:code_end],begin));fixups=[]
for i in ins:
 if i.disp_size==4 and any(o.type==capstone.CS_OP_MEM and o.mem.base==capstone.x86.X86_REG_RIP for o in i.operands):
  target=i.address+i.size+i.disp
  # Only the local socket-count switch uses the clone's image-base view.
  if i.address==0x307e14:assert target==0;target=0xffffffff
  fixups.append((i.address-begin,i.disp_offset,i.size,target))
 if i.group(capstone.CS_GRP_CALL) or i.group(capstone.CS_GRP_JUMP):
  if i.operands and i.operands[0].type==capstone.CS_OP_IMM:
   target=i.operands[0].imm
   if not begin<=target<end:assert i.imm_size==4;fixups.append((i.address-begin,i.imm_offset,i.size,target))
site=next(i for i in ins if i.address==0x306dbb);assert site.size==5
entry=next(e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress==begin)
assert entry.EndAddress==end
u=entry.UnwindData;n=4+((data[u+2]+1)&~1)*2;assert data[u]>>3==3
unwind=bytearray(data[u:u+n+8]);handler=struct.unpack_from('<I',unwind,n)[0];struct.pack_into('<I',unwind,n,0x3100)
lines=['#pragma once','#include <array>','#include <cstdint>','namespace CubeVisual::UseNativeData {',
 'inline constexpr uint32_t Begin=0x306c50,Size=0x1bec,Append=0x16b;',
 'struct Fixup {uint16_t instruction;uint8_t displacement,length;uint32_t target;};','inline constexpr Fixup Fixups[]{']
lines += [f'    {{0x{a:x},{b},{c},0x{d:x}}},'for a,b,c,d in fixups]
lines += ['};','struct Signature {uint32_t rva;uint64_t hash;};','inline constexpr Signature CopySignatures[]{']
for address in (0x207610,0x204dc0,0x2f5770,0x2d7480,0x209c60,0x2f8660,0x2f8740):
 value=14695981039346656037
 for byte in data[address:address+32]:value=((value^byte)*1099511628211)&0xffffffffffffffff
 lines += [f'    {{0x{address:x},0x{value:016x}ULL}},']
lines += ['};',f'inline constexpr uint32_t Handler=0x{handler:x};',f'inline constexpr std::array<uint8_t,{len(unwind)}> Unwind{{'+','.join(f'0x{b:02x}'for b in unwind)+'};','}']
# Grow RSP and RBP together by 0x200: all existing locals, call arguments,
# cookie offset and instruction lengths remain unchanged. Put the widened
# primary/state arrays in the new space below the saved registers.
wide=[]
for i in ins:
 for operand in i.operands:
  if operand.type==capstone.CS_OP_MEM and operand.mem.base==capstone.x86.X86_REG_RBP and 0x150<=operand.mem.disp<=0x180:
   old=operand.mem.disp
   value=0x1640+old-0x150 if old<0x168 else (0x1680+old-0x168 if old<0x180 else 0x16a0)
   assert i.disp_size==4;wide.append((i.address-begin+i.disp_offset,value))
for address,offset,value in ((0x306c5d,4,-0x1828),(0x306c65,1,0x1928),(0x3085e7,3,0x1928)):
 wide.append((address-begin+offset,value))
assert data[0x307ef9:0x307efc]==bytes.fromhex('83f903')
assert data[0x3083bb:0x3083bf]==bytes.fromhex('4183fe03')
assert data[0x307a52:0x307a55]==bytes.fromhex('83f912')
lines.pop()
lines+=['// Seven-output invocation only. Original BIN records and native functions stay unchanged.',
 'struct WidePatch {uint16_t offset;int32_t value;};','inline constexpr WidePatch WidePatches[]{']
lines += [f'    {{0x{offset:x},{value}}},'for offset,value in wide]
lines += ['};','inline constexpr uint16_t WideCounts[]{0x12ab,0x176e};',
 'inline constexpr uint32_t WideOutputs=0x1640,WideStates=0x1680;',
 'inline constexpr uint16_t WideSocketCount=0xe04;','}']
Path('d2rhud/plugin/CubeUseNativeData.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Generated',len(fixups),'native output fixups and exact GS/unwind data')
