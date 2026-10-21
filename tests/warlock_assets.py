"""Class-only compatibility pass for archived Warlock animation metadata."""
import json
import pathlib
import re
import shutil
import subprocess

CLASSES=('amazon','sorceress','necromancer','paladin','barbarian','druid','assassin')
CREATION_STATES={'CharSel_Unselected','CharSel_ToSelected','CharSel_Selected',
                 'CharSel_FromSelected','CE_Fidget01','CE_Fidget02'}

def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def types(value):
    result=set()
    if isinstance(value,dict):
        if 'type' in value:result.add(value['type'])
        for child in value.values():result.update(types(child))
    elif isinstance(value,list):
        for child in value:result.update(types(child))
    return result

def paths(value):
    if isinstance(value,str) and value.startswith('data/'):yield value
    elif isinstance(value,dict):
        for child in value.values():yield from paths(child)
    elif isinstance(value,list):
        for child in value:yield from paths(child)

def class_definitions(new,destination):
    state='data/hd/character/player/warlock/warlock_state_machine.json'
    for name in ['warlock.json','warlock.frontend']:
        rel=pathlib.Path('hd/character/player')/name
        model=load(new/rel)
        for entity in model['entities']:
            entity['components']=[c for c in entity.get('components',[]) if c['type'] not in ['WarlockEffectsDefinitionComponent','DefinitionPreloadComponent']]
        model['entities']=[e for e in model['entities'] if e.get('name')!='entity_vfx']
        model['dependencies']['particles']=[]
        # Loading a visible model is not sufficient: its initial animation
        # request is discarded when the state-machine resource is still pending.
        # Defer spells, but preload the class graph before the preview is ready.
        model['dependencies']['json']=[{'path':state}]
        model['dependencies']['textures']=[v for v in model['dependencies'].get('textures',[]) if '/fresnel/' not in v['path']]
        target=destination/rel;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(json.dumps(model,separators=(',',':')),encoding='utf-8')

def material_dependencies(old,new,destination,inspect_tool):
    references=set()
    models=sorted((new/'hd/character/player/warlock').glob('*_lod*.model'))
    for model in models:
        report=subprocess.check_output([str(inspect_tool),str(model)],text=True,encoding='utf-8',errors='strict')
        # ReflectanceTexture is a string in shader ExtendedData, outside the
        # Granny texture/map list. Audit both kinds of material dependency.
        references.update(re.findall(r'\(StringMember,0\) = (data/[^\r\n]+)',report))
    copied=[]
    for reference in sorted(references):
        rel=pathlib.Path(reference.removeprefix('data/'))
        if (destination/rel).is_file() or (old/rel).is_file():continue
        if not (new/rel).is_file():raise ValueError('Missing model dependency: '+reference)
        target=destination/rel;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(new/rel,target);copied.append(str(rel))
    return {'models':len(models),'references':len(references),'copied':copied}

def sanitize(old,new,destination,inspect_tool=None):
    old,new,destination=map(pathlib.Path,(old,new,destination))
    if destination.resolve() in [old.resolve(),new.resolve()]:
        raise ValueError('The archive must remain separate from staged assets')
    class_definitions(new,destination)
    # Restore the unmodified archived materials after the earlier albedo-only
    # experiment. Hair colour also depends on shared reflectance lookup tables.
    for lod in range(5):
        rel=pathlib.Path(f'hd/character/player/warlock/head_lod{lod}.model')
        shutil.copy2(new/rel,destination/rel)
    known=set()
    for name in CLASSES:
        for rel in [f'hd/character/player/{name}/{name}_state_machine.json',f'hd/character/player/{name}/animation/combined.timelines']:
            known.update(types(load(old/rel)))
    def available(path):
        rel=path.removeprefix('data/')
        for root in (destination,old):
            p=root/rel
            if p.is_file():return True
            if p.suffix=='.model' and p.with_name(p.stem+'_lod0.model').is_file():return True
        return False
    removed=[]
    def clean(value):
        if isinstance(value,dict):
            kind=value.get('type')
            if kind and kind not in known:
                removed.append(kind);return None
            return {key:child for key,v in value.items() if (child:=clean(v)) is not None}
        if isinstance(value,list):return [child for v in value if (child:=clean(v)) is not None]
        return value
    reports=[]
    for rel in ['hd/character/player/warlock/warlock_state_machine.json',
                'hd/character/player/warlock/animation/combined.timelines',
                'hd/character/player/warlock/animation/weapon/combined.timelines']:
        source=new/rel
        if not source.is_file():continue
        original=load(source)
        document=clean(original)
        if 'dependencies' in document:
            for key,entries in document['dependencies'].items():
                if key!='animations':document['dependencies'][key]=[v for v in entries if available(v['path'])]
        # Missing 3.1 spell/selection effects must not block the entire packed
        # animation set in the 2.4 client. Keep its supported sound, movement,
        # equipment and release-point events and the original clip timings.
        for timeline in document.get('timelines',{}).values():
            timeline['animationEvents']=[event for event in timeline.get('animationEvents',[]) if all(available(p) for p in paths(event))]
        if 'states' in document:
            deferred={'SKILL_CLEAVE','SKILL_CLEAVE_2','SKILL_MIRRORED_BLADES','SKILL_MIRRORED_BLADES_2','SKILL_BIND_DEMON_START','SKILL_BIND_DEMON_LOOP'}
            excluded={state['stateId'] for state in document['states'] if state.get('_name') in deferred}
            document['states']=[state for state in document['states'] if state['stateId'] not in excluded]
            document['transitions']=[group for group in document['transitions'] if group['from'] not in excluded]
            for group in document['transitions']:group['settings']=[item for item in group['settings'] if item['to'] not in excluded]
            # The 2.4 preview can request a weapon binding before selection has
            # normalized its equipment. Newer metadata explicitly contains
            # empty bindings; give these creation-only states their existing
            # full-body unarmed clip instead of evaluating an empty pose.
            animations={item['name'] for item in document['animations']}
            for state in document['states']:
                if state.get('_name') not in CREATION_STATES:continue
                bindings=state['animationBindings']
                fallback=bindings['hth']
                if not fallback or not all(clip in animations for clip in fallback):
                    raise ValueError('Missing creation animation: '+state['_name'])
                for weapon,clips in bindings.items():
                    if not clips:bindings[weapon]=list(fallback)
        target=destination/rel;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(json.dumps(document,separators=(',',':')),encoding='utf-8')
        assert types(document)<=known,rel
        reports.append(rel)
    materials=material_dependencies(old,new,destination,inspect_tool) if inspect_tool else None
    return {'files':reports,'removed_unsupported_types':sorted(set(removed)),'material_dependencies':materials}

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('old');parser.add_argument('new');parser.add_argument('destination')
    parser.add_argument('--inspect-tool',type=pathlib.Path,required=True)
    args=parser.parse_args()
    print(json.dumps(sanitize(args.old,args.new,args.destination,args.inspect_tool),indent=2))
