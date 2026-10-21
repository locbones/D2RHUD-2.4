"""Validate both options' data against a read-only real mod source."""
import hashlib
import importlib.util
import io
import csv
import json
from pathlib import Path
import sys
import tempfile

spec=importlib.util.spec_from_file_location('act6',Path(__file__).resolve().parents[1]/'tools/stage-act6.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
source=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='stage-act6-test-') as temporary:
    output=Path(temporary)/'data'
    manifest=module.stage(source,output)
    assert manifest['levels']==list(range(240,249))
    assert len(manifest['quest_labels'])==6 and len(manifest['native_layouts'])==20
    town=manifest['town_content']
    assert town['name']=="Dawn's Refuge"
    for relative,digest in town['assets'].items():
        assert hashlib.sha256((output/relative).read_bytes()).hexdigest()==digest
    for entry in manifest['switch_files']:
        relative=entry['path'];original=(source/relative).read_bytes()
        assert (output/relative).read_bytes()==original
        assert (output/entry['disabled']).read_bytes()==original
        assert hashlib.sha256(original).hexdigest()==manifest['source_sha256'][relative]
        if relative in manifest['native_layouts']:
            layout=json.loads((output/entry['enabled']).read_text(encoding='utf-8'))
            tabs=next(c for c in layout['children'] if c.get('name')=='Tabs')
            assert tabs['fields']['tabCount']==6 and tabs['fields']['textStrings'][-1]=='VI'
            if 'quest' in relative:
                tab=next(c for c in layout['children'] if c.get('name')=='Tab5')
                buttons=[c['name'] for c in tab['children'] if c.get('type')=='QuestLogButtonWidget']
                assert buttons==[f'Quest{n}' for n in range(27,33)]
    def rows(path):return list(csv.DictReader(io.StringIO(path.read_text(encoding='utf-8-sig')),delimiter='\t'))
    enabled=output/'Pandemonium/ActVI/enabled'
    original=rows(source/'global/excel/levels.txt');levels=rows(enabled/'global/excel/levels.txt')
    assert levels[:len(original)]==original
    additions=levels[len(original):]
    assert len(additions)==9
    assert [r['Waypoint'] for r in additions]==[str(i) for i in range(39,48)]
    assert all(r['Act']=='5' for r in additions)
    assert additions[0]['LevelType']=='26' and additions[0]['SizeX']=='48' and additions[0]['SizeY']=='40'
    presets=rows(enabled/'global/excel/lvlprest.txt')
    town_preset=next(r for r in presets if r.get('LevelId')=='240')
    assert town_preset['File1']=='act6/refuge/dawnsrefuge.ds1'
    assert town_preset['Scan']=='1' and town_preset['Pops']=='0'
    by_id={int(r['Id']):r for r in original if r.get('Id')}
    for added,cloned in zip(additions,module.CLONES):
        for n in range(8):
            assert added[f'Vis{n}']=='0'
            assert added[f'Warp{n}']==by_id[cloned][f'Warp{n}']
    acts=[r for r in rows(enabled/'global/excel/actinfo.txt') if r.get('act')]
    assert [r['act'] for r in acts]==list('123456')
    assert acts[5]['commonactcof']==acts[3]['commonactcof']
    automap=rows(enabled/'global/excel/automap.txt')
    new_walls=[r for r in automap if r.get('*Type1')=='Refuge boundary']
    assert [(r['TileName'],r['Style'],r['Cel1']) for r in new_walls]==[('wl','1','61'),('wr','1','60')]
    assert [r for r in automap if r not in new_walls]==rows(source/'global/excel/automap.txt')
    for entry in manifest['switch_files']:
        for key in ('enabled','disabled'):
            value=14695981039346656037
            for byte in (output/entry[key]).read_bytes():value=((value^byte)*1099511628211)&0xffffffffffffffff
            assert value==entry[key+'_hash']
print('Five-act originals, six-act tables, nine native waypoints, six quest labels and 20 native layouts validated; source unchanged.')
