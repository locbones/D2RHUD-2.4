"""Author Dawn's Refuge; DS1 collision and HD placement share one layout.

Usage: create-act6-town.py EXTRACTED_DATA NEW_OUTPUT
Uses stock game art, never changes an existing town or an archive asset.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import math
import struct

WIDTH, HEIGHT = 49, 41  # DS1 includes the last boundary row/column.
DS1_PATH = 'global/tiles/act6/refuge/dawnsrefuge.ds1'
SCENE_PATH = 'hd/env/preset/act6/refuge/dawnsrefuge.json'
FLOOR_MODEL = 'data/hd/env/model/expansion/wildtemple/expansion_wildtemple_temple/temple_floor01.model'
MODELS = {
    'wall': 'data/hd/env/model/act4/fort/act4_fort_walls/wall01.model',
    'smith': 'data/hd/env/model/act4/fort/act4_fort_huts/hut_blacksmith01.model',
    'merchant': 'data/hd/env/model/act4/fort/act4_fort_huts/hut_merchant01.model',
    'column': 'data/hd/env/model/act4/fort/act4_fort_exterior/ext_column01.model',
}
# Measured from stock LOD0 geometry using the Granny decoder, not guessed pivots.
BOUNDS = {
    'wall': ((-2.5412312, 0., -18.500006), (.8768123, 13.078223, 0.)),
    'smith': ((-11.169151, .0094835, -38.84309), (19.230097, 13.460594, .8428431)),
    'merchant': ((-10.153578, -.0204346, -39.387035), (-.4567728, 13.402675, 1.1347197)),
    'column': ((-5.9261074, 13.808434, -4.0095077), (-2.3494446, 32.455418, -.1040016)),
}
# All coordinates below are DS1 tiles, not world units. The central court has
# broad approaches; buildings and their collision never cover service points.
SOLIDS = [(2, 2, 17, 7), (32, 2, 47, 7), (2, 32, 17, 39), (32, 32, 47, 39)]
SERVICES = [
    # Use the freestanding Act IV waypoint (238), not the fortress-only
    # object (398), whose neutral COF expects the original preset artwork.
    ('Waypoint', 2, 0, 24, 20),
    ('Stash', 2, 2, 28, 23),
    ('Halbu', 1, 11, 12, 11),
    ('Jamella', 1, 10, 36, 11),
    ('Cain', 1, 25, 20, 30),
    ('Tyrael', 1, 7, 24, 9),
]


def walkable(x, y):
    return (2 <= x < 47 and 2 <= y < 39 and (x,y) not in ((18,8),(30,8))
            and not any(a <= x < c and b <= y < d for a,b,c,d in SOLIDS))


def encode_ds1():
    refs = ['act4/fort/plaza.dt1', 'act4/fort/interior.dt1', 'act4/fort/exterior.dt1']
    out = bytearray(struct.pack('<6I', 18, WIDTH-1, HEIGHT-1, 3, 0, len(refs)))
    for ref in refs:
        out += ('\\d2\\data\\global\\tiles\\'+ref.replace('/', '\\')).encode() + b'\0'
    out += struct.pack('<2I', 2, 1)
    walls, orient, markers, marker_orient, floor = [[0]*(WIDTH*HEIGHT) for _ in range(5)]
    for y in range(HEIGHT):
        for x in range(WIDTH):
            i = y*WIDTH+x
            if walkable(x,y):
                # Plain passable plaza tile; varied broad bands distinguish
                # service courts in legacy without random collision variants.
                floor[i] = 0xc2 | ((1 if x in range(20,29) or y in range(17,25) else 0) << 8)
            elif any(walkable(x+dx,y+dy) for dx,dy in [(1,0),(-1,0),(0,1),(0,-1)]):
                floor[i] = 0xc2 | 0x20000
                walls[i] = 0xc1 | (1 << 20)  # native fortress interior wall
                orient[i] = 1 if walkable(x,y+1) or walkable(x,y-1) else 2
    # Native tile 11 is the town entry point, independent of the waypoint.
    markers[24*WIDTH+24] = 0x82100089
    marker_orient[24*WIDTH+24] = 10
    for layer in (walls,orient,markers,marker_orient,floor,[0]*(WIDTH*HEIGHT)):
        out += struct.pack('<'+'I'*len(layer),*layer)
    out += struct.pack('<I',len(SERVICES))
    for _,kind,index,x,y in SERVICES:
        out += struct.pack('<5I',kind,index,x*5+2,y*5+2,0)
    out += struct.pack('<I',0)  # NPC path count; stationary service bays.
    return bytes(out)


def transform(name, position, scale=(1,1,1), angle=0):
    return dict(type='TransformDefinitionComponent',name=name+'_transform',
                position=dict(zip('xyz',position)),scale=dict(zip('xyz',scale)),
                orientation=dict(x=0.,y=math.sin(angle/2),z=0.,w=math.cos(angle/2)),inheritOnlyPosition=False)


def build(archive, output):
    if output.exists():raise ValueError('Output must be a new directory')
    original=json.loads((archive/'hd/env/preset/act4/fort/fortress.json').read_text())
    entities=[]
    def entity(name,components):
        e=dict(type='Entity',name=name,id=0x61000000+len(entities),components=components)
        entities.append(e);return e
    def model(name,filename,position,scale=(1,1,1),angle=0):
        return entity(name,[transform(name,position,scale,angle),dict(type='ModelDefinitionComponent',
            name=name+'_model',filename=filename,visibleLayers=1,lightMask=19,shadowMask=3,
            ghostShadows=False,floorModel=False,terrainBlendEnableYUpBlend=False,terrainBlendMode=1),
            dict(type='ModelPlatformTierComponent',name=name+'_tier',visibilityTier=0,shadowTier=0)])
    def fitted(name,kind,rect,height=None):
        x,y,w,h=rect;lo,hi=BOUNDS[kind]
        scale=[w*10/(hi[0]-lo[0]),(height/(hi[1]-lo[1]) if height else 1.),h*10/(hi[2]-lo[2])]
        position=[x*10-lo[0]*scale[0],-lo[1]*scale[1],y*10-lo[2]*scale[2]]
        return model(name,MODELS[kind],position,scale)
    # The stock temple floor has a verified continuous 20x50 core. A 2x5
    # tile lattice covers every walkable subtile; overlap stays beneath walls.
    # Nonzero Y scale avoids singular transforms, with relief below 0.002.
    for y in range(2,39,5):
        for x in range(2,47,2):
            if not any(walkable(a,b) for b in range(y,min(y+5,39)) for a in range(x,min(x+2,47))):continue
            e=model(f'paving_{x}_{y}',FLOOR_MODEL,(x*10+10,.0013,y*10+25),(1,.001,1))
            e['components'].append(dict(type='TerrainDefinitionComponent',name=e['name']+'_terrain'))
    # Solid wall belts use exactly the DS1 blocked cells. Doorways are absent
    # in this waypoint town, so there are no unbound stair or exit markers.
    for y in range(1,40):
        for x in range(1,48):
            if walkable(x,y):continue
            if any(walkable(x+dx,y+dy) for dx,dy in [(1,0),(-1,0),(0,1),(0,-1)]):
                fitted(f'rampart_{x}_{y}','wall',(x,y,1,1),10)
    fitted('forge_shelter','smith',(5,3,9,3))
    fitted('market_shelter','merchant',(34,3,9,3))
    fitted('archive_shelter','merchant',(5,34,9,3))
    fitted('watch_shelter','smith',(34,34,9,3))
    for x,y in [(3,3),(45,3),(3,37),(45,37),(18,8),(30,8)]:
        fitted(f'pillar_{x}_{y}','column',(x,y,1,1),18)
    # Restrict static decoration to blocked footprints. Native objects render
    # their own HD models (waypoint, stash, NPCs), never duplicate them here.
    deps=copy.deepcopy(original['dependencies'])
    temple=json.loads((archive/'hd/env/preset/expansion/wildtemple/tempenter.json').read_text())
    for key,values in temple['dependencies'].items():
        present={v['path'].lower() for v in deps.get(key,[]) if 'path' in v}
        deps.setdefault(key,[]).extend(copy.deepcopy(v) for v in values if v.get('path','').lower() not in present)
    deps['models']=[dict(path=p) for p in sorted({FLOOR_MODEL,*MODELS.values()})]
    deps['particles']=[]
    # Original scenes list generated biome texture names which are not files.
    # Keep the biome JSON and its real texture dependencies, without stale
    # generated-texture requests from the two donor presets.
    deps['textures']=[v for v in deps.get('textures',[]) if (archive/v['path'].removeprefix('data/')).is_file()]
    scene=dict(type='Preset',name='DawnsRefuge',dependencies=deps,entities=entities,
               terrain=copy.deepcopy(entities[0]),biomeFilename=original['biomeFilename'],
               perTileBiomeOverrides=[],specialTiles={})
    files={DS1_PATH:encode_ds1(),SCENE_PATH:(json.dumps(scene,indent=2)+'\n').encode()}
    for rel,data in files.items():
        p=output/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    manifest=dict(name="Dawn's Refuge",width=WIDTH-1,height=HEIGHT-1,level_type=26,dt1_mask=127,
                  ds1=DS1_PATH,scene=SCENE_PATH,entry=[24,24],services=SERVICES,solid_rectangles=SOLIDS,
                  assets={p:hashlib.sha256(b).hexdigest() for p,b in files.items()},
                  stock_art=True,hd_entities=len(entities))
    (output/'town.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # A small authored content preview, not a screenshot of runtime acceptance.
    from PIL import Image,ImageDraw
    im=Image.new('RGB',(WIDTH*13,HEIGHT*13+42),(19,19,24));d=ImageDraw.Draw(im)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if walkable(x,y):d.rectangle((x*13,y*13,x*13+12,y*13+12),fill=(112,104,87) if x in range(20,29) or y in range(17,25) else (66,65,63))
    for name,_,_,x,y in SERVICES:
        d.ellipse((x*13,y*13,x*13+10,y*13+10),fill=(219,174,77));d.text((x*13-20,y*13+12),name,fill=(240,223,182))
    d.text((12,HEIGHT*13+12),"DAWN'S REFUGE - authored layout",fill=(226,197,140))
    im.save(output/'layout.png')
    return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();print(json.dumps(build(args.archive,args.output),indent=2))
