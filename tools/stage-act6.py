"""Prepare an isolated Act VI data prototype. Does NOT install or enable it.

The output is an overlay, not a complete mod. Native world, UI and travel hooks
must be validated before these files can be used by the game.
"""
import argparse
import copy
import csv
import hashlib
import io
import json
import re
from pathlib import Path

CLONES = (103, 113, 115, 118, 123, 129, 35, 48, 57)
NAMES = ("Town", "Frozen Passage", "Glacial Depths", "Crystal Cavern",
         "Forgotten Temple", "Worldstone Ruins", "Catacombs", "Sewers", "Burial Halls")


def relaxed_json(text):
    # Keep quoted strings intact while removing the game's comments. Then
    # remove trailing commas outside strings, which the native parser accepts.
    token = re.compile(r'"(?:\\.|[^"\\])*"|//[^\r\n]*|/\*[\s\S]*?\*/|,\s*(?=[}\]])')
    return json.loads(token.sub(lambda m: m[0] if m[0].startswith('"') else '', text))


def patch_layout(document, quest):
    result=copy.deepcopy(document)
    tabs=next(c for c in result['children'] if c.get('name')=='Tabs')
    fields=tabs.setdefault('fields',{})
    if fields.get('tabCount')!=5:
        raise ValueError('Expected an unmodified five-act expansion layout')
    fields.update(tabCount=6,inactiveFrames=[1]*6,activeFrames=[0]*6,
                  textStrings=['I','II','III','IV','V','VI'])
    if 'tabSize' in fields:
        fields['tabSize']['x']=fields['tabSize']['x']*5//6
    if quest:
        if any(c.get('name')=='Tab5' for c in result['children']):
            raise ValueError('Act VI tab already exists')
        old=next(c for c in result['children'] if c.get('name')=='Tab4')
        new=copy.deepcopy(old);new['name']='Tab5'
        buttons=[c for c in new['children'] if c.get('type')=='QuestLogButtonWidget']
        if len(buttons)!=6:raise ValueError('Expected six Act V button templates')
        for i,button in enumerate(buttons):
            button['name']=f'Quest{27+i}'
            button.setdefault('fields',{})['gridPosition']={'x':i%3,'y':i//3}
        result['children'].insert(result['children'].index(old)+1,new)
    return result


def stage(source, output):
    source=source.resolve();output=output.resolve()
    if not source.is_dir() or not (source/'global/excel/actinfo.txt').is_file():
        raise ValueError('Source must be an unpacked mod data directory')
    if output.exists():raise ValueError('Output must be a new directory')
    if source==output or source in output.parents or output in source.parents:
        raise ValueError('Output must be separate from the source mod')
    files={}
    source_hashes={}
    town_root=Path(__file__).resolve().parents[1]/'content/act6'
    town=json.loads((town_root/'town.json').read_text())
    town_assets={relative:(town_root/relative).read_bytes() for relative in town['assets']}
    for relative,raw in town_assets.items():
        if hashlib.sha256(raw).hexdigest()!=town['assets'][relative]:
            raise ValueError(f'Authored town asset changed without rebuilding its manifest: {relative}')

    def read(relative):
        raw=(source/relative).read_bytes()
        source_hashes[relative]=hashlib.sha256(raw).hexdigest()
        return raw.decode('utf-8-sig')

    def table(name):
        reader=csv.DictReader(io.StringIO(read(f'global/excel/{name}.txt')),delimiter='\t')
        return reader.fieldnames,list(reader)

    def emit_table(name,headers,rows):
        out=io.StringIO(newline='')
        writer=csv.DictWriter(out,headers,delimiter='\t',lineterminator='\r\n')
        writer.writeheader();writer.writerows(rows)
        files[f'global/excel/{name}.txt']=out.getvalue()

    lh,levels=table('levels');ah,acts=table('actinfo')
    acts=[r for r in acts if r.get('act')]
    if [r['act'] for r in acts]!=['1','2','3','4','5']:
        raise ValueError('Expected exactly five sequential ActInfo records')
    existing={int(r['Id']):r for r in levels if r.get('Id')}
    if any(int(r['Act'])>4 for r in existing.values()):
        raise ValueError('Source already defines additional acts')
    first=max(existing)+1
    ids={old:first+i for i,old in enumerate(CLONES)}
    if first+8>=1024:raise ValueError('Level IDs exceed audited HUD capacity')
    waypoint_ids=[int(r['Waypoint']) for r in existing.values() if r.get('Waypoint') not in ('','255')]
    if max(waypoint_ids)!=38:raise ValueError('Waypoint IDs 39..47 are not available as expected')

    used_ids=set();used_keys=set();languages=set()
    for p in sorted((source/'local/lng/strings').glob('*.json')):
        values=json.loads(p.read_text(encoding='utf-8-sig'))
        for value in values:
            used_ids.add(value['id']);used_keys.add(value['Key'])
            languages.update(k for k in value if k not in ('id','Key'))
    next_id=64010

    def label(key,value):
        nonlocal next_id
        if key in used_keys:raise ValueError(f'String key already exists: {key}')
        while next_id in used_ids:next_id+=1
        if next_id>=65535:raise ValueError('No 16-bit string ID available')
        row={'id':next_id,'Key':key,**{k:value for k in sorted(languages)}}
        used_ids.add(next_id);used_keys.add(key);next_id+=1
        return row

    level_strings=json.loads(read('local/lng/strings/levels.json'))
    quest_strings=json.loads(read('local/lng/strings/quests.json'))
    for i,(old,title) in enumerate(zip(CLONES,NAMES)):
        row=copy.deepcopy(existing[old])
        row.update(Name=f'Act 6 - {title}',Id=str(ids[old]),Act='5',
                   QuestFlag='0',QuestFlagEx='0',Quest='0',Depend='0',Waypoint=str(39+i))
        if i==0:
            row.update(LevelType=str(town['level_type']),DrlgType='2',Rain='0',
                       SoundEnv='31',Layer='110',Intensity='128')
            for suffix in ('','(N)','(H)'):
                row['SizeX'+suffix]=str(town['width']);row['SizeY'+suffix]=str(town['height'])
        key=f'ActVIArea{i}'
        for col in ('LevelName','LevelWarp','LevelEntry'):row[col]=key
        row['*StringName']=town['name'] if i==0 else f'Act VI {title}'
        # Existing exits must never send an Act VI clone into an original act.
        # Initial exploration is through native waypoints; connected geometry
        # and a town exit remain a separate runtime/content acceptance task.
        # Vis controls the destination link; Warp also describes the DS1's
        # stair/door tiles, even when there is no linked destination. Removing
        # those definitions makes native DRLG tile creation abort in 2268f0.
        # Keep the source tile definitions while disabling cross-act links.
        for n in range(8):row[f'Vis{n}']='0'
        levels.append(row);level_strings.append(label(key,town['name'] if i==0 else f'Act VI {title}'))
    emit_table('levels',lh,levels)
    acts.append({**acts[-1],'act':'6','town':'Act 6 - Town','start':'Act 6 - Town',
                 # Refuge uses Act IV service NPCs. Preload their common COFs
                 # from the same act instead of Act V's NPC animation bundle.
                 'commonactcof':acts[int(existing[CLONES[0]]['Act'])]['commonactcof'],
                 'classlevelrangestart':'Act 6 - Town','classlevelrangeend':f'Act 6 - {NAMES[-1]}',
                 **{f'waypoint{i+1}':f'Act 6 - {title}' for i,title in enumerate(NAMES)}})
    emit_table('actinfo',ah,acts)
    mh,maze=table('lvlmaze')
    maze.extend({**r,'Name':f'Act 6 - {NAMES[CLONES.index(int(r["Level"]))]}',
                 'Level':str(ids[int(r['Level'])])}
                for r in list(maze) if r.get('Level') and int(r['Level']) in ids)
    emit_table('lvlmaze',mh,maze)
    ph,presets=table('lvlprest');next_def=max(int(r['Def']) for r in presets if r.get('Def'))+1
    additions=[]
    for row in presets:
        if not row.get('LevelId') or int(row['LevelId']) not in ids:continue
        original=int(row['LevelId'])
        additions.append({**row,'Name':f'Act 6 - {NAMES[CLONES.index(original)]}',
                          'Def':str(next_def),'LevelId':str(ids[original])})
        if original==CLONES[0]:
            preset=additions[-1]
            preset.update(Files='1',File1=town['ds1'].removeprefix('global/tiles/'),
                          Dt1Mask=str(town['dt1_mask']),Pops='0',PopPad='0',Scan='1',
                          SizeX=str(town['width']),SizeY=str(town['height']))
            for n in range(2,7):preset['File'+str(n)]='0'
        next_def+=1
    if not any(r['LevelId']==str(first) for r in additions):
        raise ValueError('No town preset was cloned')
    emit_table('lvlprest',ph,presets+additions)
    # The original fortress uses a prepainted town image. Refuge follows its
    # actual DS1 geometry through the ordinary room automap callback instead.
    # Its two interior-wall orientations had no entries in the town tileset.
    automap_headers,automap=table('automap')
    for tile,cell in [('wl','61'),('wr','60')]:
        if any(r['LevelName']=='4 Town' and r['TileName']==tile and r['Style']=='1'
               and r['StartSequence']=='0' for r in automap):
            raise ValueError('Refuge automap wall mapping already exists')
        row={k:'' for k in automap_headers}
        row.update(LevelName='4 Town',TileName=tile,Style='1',StartSequence='0',EndSequence='0',
                   Cel1=cell,Cel2='-1',Cel3='-1',Cel4='-1')
        row['*Type1']='Refuge boundary'
        # Keep the level-type rows contiguous: the native compiler records a
        # start/end range for each tileset, rather than searching all rows.
        end=max(i for i,r in enumerate(automap) if r['LevelName']=='4 Town')+1
        automap.insert(end,row)
    emit_table('automap',automap_headers,automap)
    quest_labels=[]
    for q in range(6):
        rows=[label(f'ActVIQuest{q+1}Title',f'Act VI Quest {q+1}'),
              label(f'ActVIQuest{q+1}Objective',f'Prototype objective: visit Act VI {NAMES[q+1]}.'),
              label(f'ActVIQuest{q+1}Completed','Quest completed.')]
        quest_strings.extend(rows)
        quest_labels.append(dict(title=rows[0]['id'],objective=rows[1]['id'],completed=rows[2]['id']))
    files['local/lng/strings/levels.json']=json.dumps(level_strings,ensure_ascii=False,indent=4)+'\n'
    files['local/lng/strings/quests.json']=json.dumps(quest_strings,ensure_ascii=False,indent=4)+'\n'
    layouts=[]
    for path in sorted(source.rglob('*.json')):
        parts=path.relative_to(source).parts
        if len(parts)>1 and parts[0].lower()=='pandemonium' and parts[1].lower()=='actvi':continue
        name=path.name.lower()
        if name not in ('questlogpanelexpansionhd.json','waypointspanelexpansionhd.json'):continue
        relative=path.relative_to(source).as_posix()
        document=relaxed_json(read(relative))
        files[relative]=json.dumps(patch_layout(document,name.startswith('quest')),ensure_ascii=False,indent=4)+'\n'
        layouts.append(relative)
    if not layouts:raise ValueError('No native expansion layouts found')
    def fnv(data):
        value=14695981039346656037
        for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
        return value
    switch_files=[]
    for relative,text in list(files.items()):
        original=(source/relative).read_bytes()
        # Store byte-exact originals, including BOM/line endings, for the
        # disabled option. Templates live outside the game's table/UI paths.
        enabled=f'Pandemonium/ActVI/enabled/{relative}'
        disabled=f'Pandemonium/ActVI/disabled/{relative}'
        files[enabled]=text.encode('utf-8')
        files[disabled]=original
        switch_files.append(dict(path=relative,enabled=enabled,disabled=disabled,
                                 enabled_hash=fnv(text.encode('utf-8')),disabled_hash=fnv(original)))
        files[relative]=original # Default remains five acts until opted in.
    manifest=dict(version=1,enabled=True,status='prototype; runtime acceptance pending',
                  mod='RMD-MP',act=6,town=first,levels=list(ids.values()),
                  clone_sources=list(CLONES),first_waypoint=39,quest_records=list(range(42,48)),
                  quest_labels=quest_labels,travel='waypoints-only prototype',
                  native_layouts=layouts,source_sha256=source_hashes,switch_files=switch_files)
    manifest['town_content']=town
    files.update(town_assets)
    files['Pandemonium/act6.json']=json.dumps(manifest,indent=2)+'\n'
    output.mkdir(parents=True)
    for relative,text in files.items():
        target=output/relative;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(text if isinstance(text,bytes) else text.encode('utf-8'))
    return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    result=stage(args.source,args.output)
    print(f"Staged Act VI town {result['town']} and eight cloned areas; {len(result['native_layouts'])} native layouts. Not installed or enabled.")
