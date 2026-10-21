"""Check the authored DS1, native object references and HD collision agreement."""
from pathlib import Path
import collections
import importlib.util
import json
import struct
import sys

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('town',root/'tools/create-act6-town.py')
town=importlib.util.module_from_spec(spec);spec.loader.exec_module(town)
content=root/'content/act6';raw=(content/town.DS1_PATH).read_bytes();at=0
def u():
    global at
    value=struct.unpack_from('<I',raw,at)[0];at+=4;return value
assert [u() for _ in range(6)]==[18,48,40,3,0,3]
for _ in range(3):at=raw.index(0,at)+1
assert u()==2 and u()==1
layers=[]
for _ in range(6):
    layers.append(struct.unpack_from('<'+'I'*(49*41),raw,at));at+=49*41*4
objects=[]
for _ in range(u()):objects.append(tuple(u() for _ in range(5)))
assert u()==0 and at==len(raw)
assert objects==[(kind,index,x*5+2,y*5+2,0) for _,kind,index,x,y in town.SERVICES]
assert objects[:2]==[(2,0,122,102,0),(2,2,142,117,0)]
for y in range(41):
    for x in range(49):
        value=layers[4][y*49+x]
        assert (bool(value&255) and not bool(value&0x20000))==town.walkable(x,y)
assert [(i%49,i//49,v) for i,v in enumerate(layers[2]) if v]==[(24,24,0x82100089)]
assert layers[3][24*49+24]==10
# Every service and every clear tile belongs to one connected walkable town.
todo=collections.deque([(24,24)]);seen=set(todo)
while todo:
    x,y=todo.popleft()
    for p in [(x+1,y),(x-1,y),(x,y+1),(x,y-1)]:
        if p not in seen and town.walkable(*p):seen.add(p);todo.append(p)
assert all((x,y) in seen for _,_,_,x,y in town.SERVICES)
assert len(seen)==sum(town.walkable(x,y) for y in range(41) for x in range(49))
scene=json.loads((content/town.SCENE_PATH).read_text());entities=scene['entities']
assert len({e['id'] for e in entities})==len(entities)
models={v['path'].lower() for v in scene['dependencies']['models']}
for e in entities:
    transform=next(c for c in e['components'] if c['type']=='TransformDefinitionComponent')
    model=next(c for c in e['components'] if c['type']=='ModelDefinitionComponent')
    assert model['filename'].lower() in models
    if e['name'].startswith('rampart_'):
        _,x,y=e['name'].split('_');assert not town.walkable(int(x),int(y))
    assert all(v>0 for v in transform['scale'].values())
assert not any(c['type']=='VfxDefinitionComponent' for e in entities for c in e['components'])
if len(sys.argv)>1:
    archive=Path(sys.argv[1]);tiles={}
    # Unlike the fortress-only neutral waypoint, this freestanding COF has
    # real local selection bounds. Both graphic modes use the same DS1 unit.
    cof=(archive/'global/objects/wv/cof/wvnuhth.cof').read_bytes()
    xmin,xmax,ymin,ymax=struct.unpack_from('<4i',cof,8)
    assert -512<xmin<xmax<512 and -512<ymin<ymax<512
    for name in ['plaza','interior','exterior']:
        data=(archive/f'global/tiles/act4/fort/{name}.dt1').read_bytes()
        n,offset=struct.unpack_from('<II',data,268)
        for i in range(n):
            p=offset+i*96;key=struct.unpack_from('<III',data,p+20)
            tiles.setdefault(key,[]).append(data[p+40:p+65])
    for i,value in enumerate(layers[4]):
        if value:
            key=(0,(value>>20)&63,(value>>8)&255);assert key in tiles
            if not value&0x20000:assert all(not any(flag&9 for flag in flags) for flags in tiles[key])
    for i,value in enumerate(layers[0]):
        if value:assert (layers[1][i],(value>>20)&63,(value>>8)&255) in tiles
    for group,dependencies in scene['dependencies'].items():
        for dependency in dependencies:
            path=dependency.get('path')
            if not path or not path.startswith('data/'):continue
            p=archive/path[5:]
            assert p.exists() or (p.suffix=='.model' and p.with_name(p.stem+'_lod0.model').exists()),path
print(f"Dawn's Refuge: {len(seen)} connected walkable tiles, native tile-11 entry, waypoint/stash/four NPCs, closed boundaries and {len(entities)} HD entities validated.")
