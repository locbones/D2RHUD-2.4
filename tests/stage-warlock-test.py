"""Stage an isolated class-only mod; never overwrites an existing mod.

Usage: python stage-warlock-test.py OLD_EXTRACTED_DATA NEW_EXTRACTED_DATA GAME_DIR
"""
import copy
import csv
import io
import json
import pathlib
import re
import shutil
import sys
from warlock_assets import sanitize

old,new,game=map(pathlib.Path,sys.argv[1:])
inspect_tool=game/'tools/D2RModelConverter/resources/native/granny_inspect.exe'
if not inspect_tool.is_file():
    raise SystemExit('Missing native model dependency inspector: '+str(inspect_tool))
zone_rel=pathlib.Path('hd/global/excel/desecratedzones.json')
zone_source=game/'Mods/TCP/TCP.mpq/data'/zone_rel
if not zone_source.is_file():
    raise SystemExit('Missing TCP runtime zone configuration: '+str(zone_source))
name='HUD-WarlockTest'
root=game/'Mods'/name/(name+'.mpq')
if root.exists():
    raise SystemExit('Refusing to overwrite existing '+str(root))
data=root/'data'
def write(rel,content):
    target=data/rel;target.parent.mkdir(parents=True,exist_ok=True)
    if isinstance(content,str):target.write_text(content,encoding='utf-8')
    else:target.write_bytes(content)
def load_json(path):
    # Preserve quoted strings while removing D2R's comments/trailing commas.
    s=path.read_text(encoding='utf-8-sig')
    s=re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/',lambda m:'' if m[0].startswith(('/',)) else m[0],s,flags=re.S)
    s=re.sub(r',\s*([}\]])',r'\1',s)
    return json.loads(s)
def table(path):
    return list(csv.reader(io.StringIO(path.read_text(encoding='utf-8-sig')),delimiter='\t'))
def write_table(rel,rows):
    out=io.StringIO();csv.writer(out,delimiter='\t',lineterminator='\n').writerows(rows);write(rel,out.getvalue())

# The test launcher uses the TCP-enabled runtime. Its zone loader requires
# this mod-local file even when the class tables came from the 2.4 archive.
# Preserve the supplied JSON comments and values verbatim.
write(zone_rel,zone_source.read_bytes())

# Retain all original table row IDs and compatible column schemas.
for filename in ['playerclass.txt','plrtype.txt','charstats.txt']:
    rows=table(old/'global/excel'/filename);source=table(new/'global/excel'/filename)
    header=rows[0];row=next(r for r in source[1:] if r and r[0]=='Warlock')
    assert header==source[0],filename+' schema changed'
    if filename=='charstats.txt':
        base=next(r for r in rows[1:] if r and r[0]=='Necromancer')
        row=list(row)
        # Native Attack and common actions only. No new skill function IDs,
        # items, weapons or skill tree UI are introduced by this prototype.
        for i,column in enumerate(header):
            if column=='StartSkill':row[i]=''
            elif column.startswith('Skill ') or column.startswith('item') or column in ['baseWClass','StrAllSkills','StrSkillTab1','StrSkillTab2','StrSkillTab3','StrClassOnly']:
                row[i]=base[i] if i<len(base) else ''
    rows.append(row);write_table('global/excel/'+filename,rows)

panel=load_json(old/'global/ui/layouts/charactercreatepanelhd.json')
archive_panel=load_json(new/'global/ui/layouts/charactercreatepanelhd.json')
def walk(node):
    yield node
    for child in node.get('children',[]):yield from walk(child)
widgets={x['name']:x for x in walk(panel) if x.get('type')=='HDCharacterSelectorWidget'}
archive_widgets={x['name']:x for x in walk(archive_panel) if x.get('type')=='HDCharacterSelectorWidget'}
assert len(widgets)==7 and 'Warlock' in archive_widgets
for key,widget in widgets.items():
    source=archive_widgets[key]['fields']
    for field in ['rect','player/position','player/yawDegrees']:
        if field in source:widget['fields'][field]=copy.deepcopy(source[field])
warlock=copy.deepcopy(widgets['Sorceress']);warlock['name']='Warlock'
for field in ['rect','player/position','player/yawDegrees','player/playerClass']:
    warlock['fields'][field]=copy.deepcopy(archive_widgets['Warlock']['fields'][field])
# Remove archive-specific starting equipment (grimoire) until skills are ported.
for field in list(warlock['fields']):
    if field.startswith('player/') and field not in ['player/playerClass','player/position','player/yawDegrees']:
        del warlock['fields'][field]
panel['children'].append(warlock)
write('global/ui/layouts/charactercreatepanelhd.json',json.dumps(panel,indent=2))
ui=load_json(old/'local/lng/strings/ui.json')
ui_keys={row['Key'] for row in ui}
archive_ui=load_json(new/'local/lng/strings/ui.json')
ui.extend(row for row in archive_ui if row.get('Key') in ['Warlock','warlockstr','strWarlockDesc'] and row['Key'] not in ui_keys)
write('local/lng/strings/ui.json',json.dumps(ui,ensure_ascii=False,indent=2))

# Copy only Warlock's model/skeleton/animations/textures and 2D components.
for rel in ['hd/character/player/warlock','global/chars/wk']:
    assert (new/rel).is_dir(),rel
    shutil.copytree(new/rel,data/rel)

# Lod metadata uses asset paths rather than item/skill row indices. The archive
# registers Warlock's otherwise-unlisted *_lodN models here.
for rel in ['hd/model_lod_desc.bin','hd/model_lod_desc.json']:
    write(rel,(new/rel).read_bytes())
# Cache only new Warlock textures, retaining the old descriptors for old assets.
textures=load_json(old/'hd/texture_desc_cache.json')
new_textures=load_json(new/'hd/texture_desc_cache.json')
known={x['filename'] for x in textures}
textures.extend(x for x in new_textures if '/warlock/' in x['filename'].lower() and x['filename'] not in known)
write('hd/texture_desc_cache.json',json.dumps(textures,separators=(',',':')))
for rel in ['global/animdata.d2','global/chars_cof.d2','global/allcofs.bin']:
    write(rel,(new/rel).read_bytes())
sanitize(old,new,data,inspect_tool)
write('Pandemonium/extended_classes.json',json.dumps({'version':1,'warlock':True},indent=2))
root.parent.mkdir(parents=True,exist_ok=True)
(root/'modinfo.json').write_text(json.dumps({'name':name,'savepath':name+'/'}),encoding='utf-8')
print(root)
print('Class-only test mod; separate saves; existing RMD-MP files untouched.')
