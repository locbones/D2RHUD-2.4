"""Update verification, safe extraction and operation-boundary adoption on fixtures."""
from contextlib import contextmanager
import hashlib
import io
import json
from pathlib import Path
import runpy
import tempfile
import types
import zipfile
import sys

module = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'tools/marketplace/companion-update.py'))


def refuses(callback):
    try:
        callback()
    except (ValueError, KeyError, zipfile.BadZipFile):
        return
    raise AssertionError('Unsafe update accepted')


if len(sys.argv) > 1:
    signed = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
    envelope = json.dumps(signed['envelope']).encode()
    assert module['verify_envelope'](envelope, signed['modulus'])['version'] == '0.1.14'
    altered = dict(signed['envelope']);altered['payload'] = altered['payload'][:-2] + 'AA'
    refuses(lambda: module['verify_envelope'](json.dumps(altered).encode(), signed['modulus']))
    refuses(lambda: module['verify_envelope'](envelope))  # fixture key is not the publisher key

with tempfile.TemporaryDirectory(prefix='market-update-fixture-') as folder:
    root = Path(folder).resolve()
    entries = {n: b'fixture' for n in ('python.exe', 'pythonw.exe', 'custody-save.exe', 'custody-agent.py', 'custody-workspace.py', 'sync-marketplace.py', 'companion-host.py', 'companion-update.py', 'install-native-layout.py')}
    manifest = {'files': {n: hashlib.sha256(v).hexdigest() for n, v in entries.items()}}
    def package(extra=None):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as archive:
            archive.writestr('manifest.json', json.dumps(manifest))
            for name, value in entries.items():archive.writestr(name, value)
            if extra:archive.writestr(*extra)
        return stream.getvalue()
    raw = package();sha = hashlib.sha256(raw).hexdigest()
    runtime = module['unpack'](raw, root / 'runtime', sha)
    assert module['unpack'](raw, root / 'runtime', sha) == runtime
    refuses(lambda: module['unpack'](package(('../outside.py', b'unsafe')), root / 'runtime', 'b' * 64))
    (runtime / 'custody-save.exe').write_bytes(b'modified')
    refuses(lambda: module['unpack'](raw, root / 'runtime', sha))
    (runtime / 'custody-save.exe').write_bytes(entries['custody-save.exe'])
    profiles=[]
    for i in range(2):
        profile=root / ('profile'+str(i));state=profile/'custody';state.mkdir(parents=True);profiles.append(profile)
        config={'owner':'owner'+str(i),'mod':'Mod'+str(i),'catalog':'family'+str(i),'state_directory':str(state),'managed_runtime':str(root/'old'), 'launcher_sha256':'unchanged-launcher','hud_sha256':'unchanged-HUD','agent_pairing':'unchanged-credentials'}
        (profile/'custody-enrollment.json').write_text(json.dumps(config));(profile/'sync-config.json').write_text(json.dumps({'managed_runtime':str(root/'old'),'token_env':'D2_MARKET_TOKEN'}))
    read=lambda p:json.loads(Path(p).read_text())
    def atomic(path, value):Path(path).write_text(json.dumps(value))
    atomic(root/'managed-runtime.json',{'runtime':str(root/'old'),'profiles':[str(p) for p in profiles]})
    pending=profiles[0]/'custody/pending.json';pending.write_text('durable recovery journal')
    sentinel=root/'untouched-stash.d2i';sentinel.write_bytes(b'save contents must never change')
    before=read(profiles[0]/'custody-enrollment.json')
    @contextmanager
    def lease(path):yield
    # A fixture updater never registers itself in Windows startup.
    module['apply'].__globals__['os']=types.SimpleNamespace(name='fixture')
    assert not module['apply'](root,runtime,lease,atomic,read)
    assert read(profiles[0]/'custody-enrollment.json')==before and pending.read_text()=='durable recovery journal'
    assert read(root/'managed-runtime.json')['runtime']==str(root/'old')
    pending.unlink()  # Fixture operation finished normally.
    assert module['apply'](root,runtime,lease,atomic,read)
    for i,profile in enumerate(profiles):
        config=read(profile/'custody-enrollment.json')
        assert config['managed_runtime']==str(runtime) and config['owner']=='owner'+str(i) and config['mod']=='Mod'+str(i)
        assert config['launcher_sha256']=='unchanged-launcher' and config['hud_sha256']=='unchanged-HUD' and config['agent_pairing']=='unchanged-credentials'
        assert read(profile/'sync-config.json')['managed_runtime']==str(runtime)
    assert sentinel.read_bytes()==b'save contents must never change'
    assert read(root/'managed-runtime.json')['runtime']==str(runtime)
with tempfile.TemporaryDirectory(prefix='market-same-version-fixture-') as folder:
    root=Path(folder)
    manifest['version']='0.1.14';manifest['build_epoch']=100
    raw=package();sha=hashlib.sha256(raw).hexdigest()
    release={'version':'0.1.14','sequence':123,'file':sha+'.zip','size':len(raw),'sha256':sha}
    globals_=module['check'].__globals__
    globals_['download']=lambda name,limit: b'envelope' if name=='manifest.json' else raw
    globals_['verify_envelope']=lambda envelope: release
    applied=[];globals_['apply']=lambda *args:applied.append(True)
    current=Path(globals_['__file__']).resolve().parent/'manifest.json'
    def document(path):
        return {'version':'0.1.14','build_epoch':200} if Path(path)==current else json.loads(Path(path).read_text())
    module['check'](root,atomic,document)
    assert not applied and not (root/'updates/manifest.json').exists()
print('Companion updates: exact signed metadata, tamper/wrong-key rejection, bounded flat verified extraction, pending-operation deferral, all-mod adoption, preserved pins/identity/credentials, untouched saves and no same-version build downgrade passed.')
