"""Compare native and server identity decoders on read-only real stash pages."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

php, helper, data, saves = [Path(p).resolve() for p in sys.argv[1:]]
repo = Path(__file__).resolve().parents[1]
rows, sources, refused = [], {}, 0
for page in saves.glob('Stash_SC_Page*.d2i'):
    sources[page] = hashlib.sha256(page.read_bytes()).hexdigest()
    scan = subprocess.run([str(helper), 'inventory', str(data), str(page)], capture_output=True, text=True)
    if scan.returncode:
        refused += 1
        continue
    def add(item):
        rows.append(item)
        for child in item['socketed_items']:
            add(child)
    for item in json.loads(scan.stdout)['items']:
        add(item)
assert rows
with tempfile.TemporaryDirectory(prefix='market-identity-parity-') as work:
    path = Path(work)/'items.json'
    path.write_text(json.dumps(rows), encoding='utf-8')
    code = "require $argv[1];$out=[];foreach(json_decode(file_get_contents($argv[2]),true) as $r)$out[]=SavedItemIdentity::read($r['item_hex']);echo json_encode($out,JSON_THROW_ON_ERROR);"
    parsed = json.loads(subprocess.check_output([str(php), '-r', code, str(repo/'tools/marketplace/server/ItemIdentity.php'), str(path)], text=True))
for native, server in zip(rows, parsed, strict=True):
    assert server, native['code']
    for field in ('code', 'saved_id', 'item_uuid', 'quality', 'index', 'level'):
        assert native[field] == server[field], (field, native['code'], native[field], server[field])
assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in sources.items())
print(json.dumps(dict(items_with_children=len(rows), complete_pages=len(sources)-refused, refused_incomplete_pages=refused,
    uuid_items=sum(bool(r['item_uuid']) for r in rows), legacy_seed_items=sum(bool(r['saved_id']) for r in rows),
    original_saves_unchanged=True, decoder_parity=True)))
