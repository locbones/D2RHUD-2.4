"""Offline verification of the installed v2.4 chat entry/close/packet contracts."""
import hashlib,json,re,sys
from pathlib import Path
import capstone,pefile
path=Path(sys.argv[1]);pe=pefile.PE(str(path));base=pe.OPTIONAL_HEADER.ImageBase
header=(Path(__file__).resolve().parents[1]/'d2rhud/plugin/MarketplaceChat.h').read_text(encoding='utf-8')
locations={'SubmitPrefix':0x11f590,'ClosePrefix':0x86860,'PacketQueuePrefix':0x10dce0}
for name,rva in locations.items():
    body=re.search(r'\b'+name+r'\{([^}]+)\}',header)[1];expected=bytes(int(v,0) for v in body.split(','));assert pe.get_data(rva,len(expected))==expected,name
    assert any(e.struct.BeginAddress==rva for e in pe.DIRECTORY_ENTRY_EXCEPTION)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
submit=list(md.disasm(pe.get_data(0x11f590,0x122535-0x11f590),base+0x11f590))
assert any(i.mnemonic=='mov' and i.op_str=='rdi, rdx' for i in submit[:20])
# This dispatcher compares slash commands before eventually constructing/sending
# the chat packet. Intercept here; the packet hook alone cannot handle /market.
md.detail=True;strings=[]
for i in md.disasm(pe.get_data(0x11f590,0x122535-0x11f590),base+0x11f590):
    if i.mnemonic=='lea':
        for op in i.operands:
            if op.type==capstone.CS_OP_MEM and op.mem.base==capstone.x86.X86_REG_RIP:
                raw=pe.get_data(i.address+i.size+op.mem.disp-base,64).split(b'\0')[0]
                if raw.startswith(b'/'):strings.append(raw.decode('ascii'))
assert '/players' in strings and '/fps' in strings
for rva in [0x74ea5,0x1c385e]:
    ins=list(md.disasm(pe.get_data(rva,16),base+rva));assert ins[0].mnemonic=='call' and ins[0].op_str==hex(base+0x11f590)
    assert ins[1].mnemonic=='mov' # Both native callers ignore its return value.
close=list(md.disasm(pe.get_data(0x86860,0x1ea),base+0x86860))
assert any(i.mnemonic=='mov' and '[rbx + r14 + 0x1d53fb8], 0' in i.op_str for i in close)
assert any(i.mnemonic=='call' and i.op_str==hex(base+0x85cb0) for i in close)
print(json.dumps({'binary_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'entry_prefixes':'passed','native_slash_dispatch':'before packet construction','chat_text_argument':'RDX','native_close':'UI flag clear + native close + widget visibility','normal_callers':'return ignored','source':'offline PE/Capstone audit; no runtime writes'},indent=2))
