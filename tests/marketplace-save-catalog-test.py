"""Semantic compatibility from the actual compiled game tables, on copies only."""
from pathlib import Path
import hashlib
import json
import importlib.util
import shutil
import struct
import subprocess
import sys
import tempfile

helper=Path(sys.argv[1]).resolve();source=Path(sys.argv[2]).resolve()
def catalog(data):
    raw=subprocess.check_output([str(helper),'save-catalog',str(data),'unused']).strip()
    return hashlib.sha256(raw).hexdigest(),json.loads(raw)
with tempfile.TemporaryDirectory(prefix='market-save-catalog-') as temp:
    data=Path(temp)/'mods/Fixture/Fixture.mpq/data'
    for name in ['weapons','armor','misc','itemstatcost','itemtypes']:
        p=data/'global/excel'/f'{name}.bin';p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source/'global/excel'/p.name,p)
    metadata=data/'D2RHUD/table-metadata.json';metadata.parent.mkdir(parents=True);shutil.copy2(source/'D2RHUD/table-metadata.json',metadata)
    before,definition=catalog(data)
    spec=importlib.util.spec_from_file_location('catalog_test_agent',Path(__file__).resolve().parents[1]/'tools/marketplace/custody-agent.py')
    agent_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(agent_module)
    saves=Path(temp)/'saves';saves.mkdir();(saves/'custody-simulation.json').write_text('{}',encoding='utf-8')
    config={'simulation':True,'state_directory':str(Path(temp)/'state'),'save_directory':str(saves),'mod_data':str(data),'mod':'Fixture','game_mode':'2.4',
            'helper':str(helper),'helper_sha256':hashlib.sha256(helper.read_bytes()).hexdigest(),'catalog':'a'*64,'catalog_protocol':2,'catalog_schema':before}
    agent=agent_module.Agent(config)
    doc=json.loads(metadata.read_text(encoding='utf-8'));doc['unrelated-note']='Changed recipe descriptions and display text'
    metadata.write_text(json.dumps(doc,indent=2),encoding='utf-8')
    assert catalog(data)[0]==before,'Unrelated metadata or JSON formatting changed compatibility'
    agent.verify()
    weapon=data/'global/excel/weapons.bin';raw=bytearray(weapon.read_bytes());raw[4+8]^=1;weapon.write_bytes(raw)
    assert catalog(data)[0]==before,'Unrelated compiled columns changed compatibility'
    agent.verify()
    row=raw[4:4+0x1b4];row[0x80:0x84]=b'ZZ9 ';raw.extend(row);struct.pack_into('<I',raw,0,struct.unpack_from('<I',raw)[0]+1);weapon.write_bytes(raw)
    added,more=catalog(data);assert added!=before and 'ZZ9 ' in more['items']
    try:
        agent.verify()
        raise AssertionError('Changed semantic schema did not wait for authenticated compatibility refresh')
    except ValueError:
        pass
    assert all(more['items'][k]==v for k,v in definition['items'].items())
    stats=data/'global/excel/itemstatcost.bin';raw=bytearray(stats.read_bytes());raw[4+31*0x144+0x15]^=1;stats.write_bytes(raw)
    assert catalog(data)[0]!=added,'Saved stat width did not change compatibility'
    if len(sys.argv)>3:
        Path(sys.argv[3]).write_text(json.dumps({'fingerprint':before,'definition':definition}),encoding='utf-8')
print('Native save catalog: ignores unrelated metadata/compiled columns, records new items, detects saved bit-width changes; no real game files modified.')
