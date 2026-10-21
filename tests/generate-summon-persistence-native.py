"""Regenerate 32-byte summon native entry signatures from the audited executable.
Usage: python tests/generate-summon-persistence-native.py D2R.exe [header]
Requires pefile. No executable code is stored or distributed.
"""
from pathlib import Path
import hashlib,re,sys
import pefile
expected='2c2ddb60a40ede3aaff51afb64e932621a03e32487687b6c64d88205fa652360'
source=Path(sys.argv[1]); assert hashlib.sha256(source.read_bytes()).hexdigest()==expected,'Unsupported executable'
image=pefile.PE(str(source)).get_memory_mapped_image()
addresses=[0x29b4c0,0x367f60,0x368a90,0x367d40,0x367af0,0x367910,0x3c5410,0x21e3a0,0x218ce0,0x21a990,0x2095a0,0x1e5620,0x2c49c0,0x209530,0x6d08c0,0x2185e0,0x1e2e00,0x29bf70,0x371690,0x368cb0,0x359950,0x369e90,0x3608d0,0x360c20,0x361cb0,0x382370]
out=['// BEGIN GENERATED SIGNATURES',f'inline constexpr std::array<Signature, {len(addresses)}> Signatures{{{{']
for address in addresses:
 h=14695981039346656037
 for value in image[address:address+32]:h=((h^value)*1099511628211)&0xffffffffffffffff
 out.append(f'    {{0x{address:x}, 0x{h:016x}ULL}},')
out+=['}};','// END GENERATED SIGNATURES']
header=Path(sys.argv[2]) if len(sys.argv)>2 else Path(__file__).resolve().parents[1]/'d2rhud/plugin/SummonPersistenceNative.h'
text=header.read_text();text=re.sub(r'// BEGIN GENERATED SIGNATURES.*?// END GENERATED SIGNATURES','\n'.join(out),text,flags=re.S)
header.write_text(text,encoding='utf-8');print('Verified executable; generated',len(addresses),'native entry signatures')


