"""Install an authored Act VI pack while preserving the target's selected mode.

Only accepts known previous/current template bytes. Backs up every replaced
file and rolls back on failure. Run with D2R closed so -txt compiles one pack.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile


def fnv(raw):
    value=14695981039346656037
    for byte in raw:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value


def install(pack,target,mode='keep'):
    pack=pack.resolve();target=target.resolve()
    new=json.loads((pack/'Pandemonium/act6.json').read_text())
    old_path=target/'Pandemonium/act6.json'
    old=json.loads(old_path.read_text()) if old_path.exists() else new
    old_files={e['path']:e for e in old['switch_files']}
    choices={}
    for entry in new['switch_files']:
        relative=entry['path'];p=target/relative;previous=old_files.get(relative,entry)
        raw=p.read_bytes();value=fnv(raw)
        if value not in {previous['enabled_hash'],previous['disabled_hash'],entry['enabled_hash'],entry['disabled_hash']}:
            raise ValueError(f'Target edited outside its templates: {p}')
        if relative in old_files:
            choices[relative]=(value in {previous['enabled_hash'],entry['enabled_hash']})
        for state in ('enabled','disabled'):
            if fnv((pack/entry[state]).read_bytes())!=entry[state+'_hash']:
                raise ValueError(f'Invalid staged template: {entry[state]}')
    on=choices['global/excel/actinfo.txt'] if mode=='keep' else mode=='enabled'
    # Do not silently flatten a half-switched installation.
    if mode=='keep' and len(set(choices.values()))!=1:
        raise ValueError('Target has mixed enabled/disabled files; choose a mode explicitly after closing D2R')
    replacements={}
    for entry in new['switch_files']:
        replacements[entry['path']]=(pack/entry['enabled' if on else 'disabled']).read_bytes()
        for state in ('enabled','disabled'):replacements[entry[state]]=(pack/entry[state]).read_bytes()
    replacements['Pandemonium/act6.json']=(pack/'Pandemonium/act6.json').read_bytes()
    for relative,expected in new['town_content']['assets'].items():
        data=(pack/relative).read_bytes()
        if hashlib.sha256(data).hexdigest()!=expected:raise ValueError(f'Invalid town asset: {relative}')
        p=target/relative
        if p.exists() and p.read_bytes()!=data:
            prior=old.get('town_content',{}).get('assets',{}).get(relative)
            if not prior or hashlib.sha256(p.read_bytes()).hexdigest()!=prior:
                raise ValueError(f'Unknown existing town asset: {p}')
        replacements[relative]=data
    changed={p:b for p,b in replacements.items() if not (target/p).exists() or (target/p).read_bytes()!=b}
    backup=Path(tempfile.mkdtemp(prefix='d2rhud-act6-install-'))
    existed=set();done=[]
    try:
        for relative in changed:
            p=target/relative
            if p.exists():
                existed.add(relative);q=backup/relative;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
        for relative,data in changed.items():
            p=target/relative;p.parent.mkdir(parents=True,exist_ok=True)
            temporary=p.with_name(p.name+'.act6-install-pending')
            temporary.write_bytes(data);os.replace(temporary,p);done.append(relative)
    except BaseException:
        for relative in reversed(done):
            p=target/relative
            if relative in existed:shutil.copy2(backup/relative,p)
            else:p.unlink()
        raise
    report=dict(target=str(target),enabled=on,changed=len(changed),backup=str(backup),files=list(changed))
    (backup/'installation.json').write_text(json.dumps(report,indent=2))
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pack',type=Path);parser.add_argument('target',type=Path)
    parser.add_argument('--mode',choices=['keep','enabled','disabled'],default='keep')
    args=parser.parse_args();print(json.dumps(install(args.pack,args.target,args.mode),indent=2))
