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
Path('d2rhud/plugin/CubeUseNativeData.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Generated',len(fixups),'native output fixups and exact GS/unwind data')
