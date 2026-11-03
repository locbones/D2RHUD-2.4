"""Read-only real D2S parsing, immutable sources and explicit stash exclusion."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

helper, data, folder = map(Path, sys.argv[1:4])
sources = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.glob('*.d2s')}
assert sources
def parse(path, ok=True):
    result = subprocess.run([str(helper.resolve()), 'character', str(data.resolve()), str(path.resolve())], capture_output=True)
    assert (result.returncode == 0) is ok, result.stderr.decode('utf-8', 'replace')
    return json.loads(result.stdout) if ok else None
def checksum(raw):
    value=0
    for i,b in enumerate(raw):
        value=((value<<1)+(value>>31)+(0 if 12<=i<16 else b)) & 0xffffffff
    return value
with tempfile.TemporaryDirectory(prefix='market-characters-') as temporary:
    copy=Path(temporary)/'character.d2s'
    for source in sources:
        raw=source.read_bytes();copy.write_bytes(raw);profile=parse(copy)
        assert 0<=profile['class_id']<=6 and profile['level']>0
        assert all(r['location']==1 for r in profile['equipment']+profile['mercenary']['equipment'])
        assert all(r['location']==2 or r['location']==0 and r['container']==1 for r in profile['inventory'])
        bad=bytearray(raw);bad[-1]^=1;copy.write_bytes(bad);parse(copy,False)
        copy.write_bytes(raw[:-1]);parse(copy,False)
        stored=next((r for r in profile['inventory'] if r['location']==0 and r['container']==1),None)
        if stored:
            item=bytes.fromhex(stored['item_hex']);at=raw.find(item);assert at>=0
            changed=bytearray(raw)
            for i in range(3):
                bit=at*8+50+i;changed[bit//8]=(changed[bit//8]&~(1<<(bit%8)))|(((5>>i)&1)<<(bit%8))
            struct.pack_into('<I',changed,12,checksum(changed));copy.write_bytes(changed);filtered=parse(copy)
            assert len(filtered['inventory'])==len(profile['inventory'])-1
            assert filtered['equipment']==profile['equipment'] and filtered['mercenary']==profile['mercenary']
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in sources.items())
print(f'Character profiles: {len(sources)} real immutable saves, checksum/truncation refusal, equipment/mercenary boundaries and personal-stash exclusion passed.')
