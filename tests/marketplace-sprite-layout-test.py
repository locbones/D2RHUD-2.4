"""Validate real sprite dimensions against native hit boxes and captions."""
from pathlib import Path
import json,struct,sys,runpy,tempfile,shutil

repo=Path(__file__).resolve().parents[1]
data=Path(sys.argv[1]);layout=json.loads((repo/'tools/marketplace/layouts/marketplace-workspace.json').read_text(encoding='utf-8'))
checked=0
def sprite(filename):
    path=(data/'hd/global/ui'/filename.replace('\\','/')).resolve().with_suffix('.sprite')
    if not path.is_file() and 'Pandemonium/Marketplace/' in path.as_posix():path=repo/'tools/marketplace/assets'/path.name
    return path
def inspect(node):
    global checked
    f=node.get('fields',{})
    if node['name'].startswith('MarketTorchClass'):
        assert node['type']=='DropdownListWidget' and f.get('visible') is False
        assert f['states']==['Any class','Amazon','Sorceress','Necromancer','Paladin','Barbarian','Druid','Assassin']
    if node['type']=='ButtonWidget' and f.get('filename')!='Blank':
        raw=sprite(f['filename']).read_bytes();fmt,width,atlas,height=struct.unpack_from('<HHII',raw,4)
        r=f['rect'];assert (r['width'],r['height'])==(width,height),(node['name'],r,width,height)
        for c in node.get('children',[]):
            if c['type']=='TextBoxWidget':
                caption=c['fields'];assert caption['rect']=={'x':12,'y':0,'width':width-24,'height':height},node['name']
                assert caption['style']['alignment']=={'h':'center','v':'center'}
                assert caption['style']['options']['lineWrap'] is False
        checked+=1
    if f.get('filename','').endswith('Shop\\Shop_Button'):raise AssertionError('Marketplace still reuses the portal icon')
    for child in node.get('children',[]):inspect(child)
inspect(layout)
shop=json.loads((repo/'tools/marketplace/layouts/stash-shop-button.json').read_text(encoding='utf-8'));inspect(shop)
for name,w,h in [('MarketplaceButton.sprite',117,116),('MarketplaceButton.lowend.sprite',58,58)]:
    raw=(repo/'tools/marketplace/assets'/name).read_bytes()
    assert raw[:4]==b'SpA1' and struct.unpack_from('<HHII',raw,4)==(31,w,w*3,h)
    assert struct.unpack_from('<I',raw,20)[0]==3 and len(raw)==40+w*h*3*4
    first=raw[40:40+w*4];assert first!=raw[40+w*4:40+w*8] and first!=raw[40+w*8:40+w*12]
for name,w in [('MarketplaceGift.sprite',64),('MarketplaceGift.lowend.sprite',32)]:
    raw=(repo/'tools/marketplace/assets'/name).read_bytes()
    assert raw[:4]==b'SpA1' and struct.unpack_from('<HHII',raw,4)==(31,w,w*3,w)
    assert len(raw)==40+w*w*3*4 and raw[43]==0, 'Gift icon must have a transparent background'
    assert max(raw[43::4])>200, 'Gift artwork must remain visible'
# Real installer fixtures: unrelated bytes and duplicate fields survive; asset
# installation is atomic/idempotent and absent artwork cannot publish a layout.
installer=runpy.run_path(str(repo/'tools/marketplace/install-native-layout.py'))
with tempfile.TemporaryDirectory(prefix='market-native-assets-') as temporary:
    root=Path(temporary);target=root/'global/ui/layouts/bankexpansionlayouthd.json';target.parent.mkdir(parents=True)
    original='{"type":"BankPanel","name":"BankExpansionLayout","fields":{"duplicate":1,"duplicate":2},"children":[{"type":"Widget","name":"Existing","fields":{}}]}'
    target.write_text(original,encoding='utf-8')
    report=installer['install'](root,backup_directory=root/'backup')
    assert report['changed']==5 and '"duplicate":1,"duplicate":2' in target.read_text(encoding='utf-8')
    assert installer['install'](root)['changed']==0
    assert (root/'Pandemonium/Marketplace/MarketplaceButton.sprite').read_bytes()==(repo/'tools/marketplace/assets/MarketplaceButton.sprite').read_bytes()
    missing=root/'missing-source';missing.mkdir()
    for name in ('stash-shop-button.json','marketplace-workspace.json','stash-sync-mask.json'):
        shutil.copy2(repo/'tools/marketplace/layouts'/name,missing/name)
    fresh=root/'fresh';file=fresh/'global/ui/layouts/bankexpansionlayouthd.json';file.parent.mkdir(parents=True);file.write_text(original,encoding='utf-8')
    try:installer['install'](fresh,source=missing,backup_directory=root/'missing-backup')
    except FileNotFoundError:pass
    else:raise AssertionError('A layout was installed without its icon')
    assert file.read_text(encoding='utf-8')==original
    namespace=installer['install'].__globals__;replace=namespace['os'].replace
    def fail_second_asset(a,b):
        if Path(b).name=='MarketplaceButton.lowend.sprite':raise OSError('Fixture asset write failure')
        return replace(a,b)
    namespace['os'].replace=fail_second_asset
    try:
        try:installer['install'](fresh,backup_directory=root/'rollback-backup')
        except OSError:pass
        else:raise AssertionError('Injected installation failure was ignored')
    finally:namespace['os'].replace=replace
    assert file.read_text(encoding='utf-8')==original and not (fresh/'Pandemonium/Marketplace/MarketplaceButton.sprite').exists()
print(f'Native artwork: {checked} actual sprite/hit-box matches, centered captions, three distinct HD/low-end states, no portal reuse, raw-preserving atomic installation and idempotency passed.')
