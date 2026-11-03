"""Verification failures must reach the native/cache status without touching saves."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

repo = Path(__file__).resolve().parents[1]
script = repo / 'tools/marketplace/custody-agent.py'
spec = importlib.util.spec_from_file_location('failed_agent', script)
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)

with tempfile.TemporaryDirectory(prefix='market-agent-failure-') as temporary:
    root = Path(temporary)
    state, saves, game = [root / name for name in ('state', 'saves', 'game')]
    data = game / 'Mods/RMD-MP/RMD-MP.mpq/data'
    for path in (state, saves, data / 'global/excel'):
        path.mkdir(parents=True)
    for name in ('weapons', 'armor', 'misc', 'itemstatcost', 'itemtypes'):
        (data / f'global/excel/{name}.bin').write_bytes(b'fixture')
    helper, launcher, hud = root / 'helper.exe', root / 'launcher.exe', game / 'd2rhud.dll'
    for path in (helper, launcher, hud):
        path.write_bytes(path.name.encode())
    cfg = dict(schema=1, enrolled=True, owner='f' * 32, state_directory=str(state), save_directory=str(saves),
        mod_data=str(data), catalog=a.catalog(data), game_directory=str(game), helper=str(helper),
        helper_sha256=a.io.digest(helper.read_bytes()), launcher_path=str(launcher),
        launcher_sha256=a.io.digest(launcher.read_bytes()), hud_sha256='0' * 64)
    enrollment = root / 'enrollment.json'
    a.io.save(enrollment, cfg)
    sentinel = saves / 'Stash_SC_Page2.d2i'
    sentinel.write_bytes(b'untouched player stash')
    pending = state / 'pending.json'
    pending.write_bytes(b'{"preserve":"pending transfer"}')
    a.io.save(state / 'live-window.json', dict(session='current-session', owner=cfg['owner'], catalog=cfg['catalog']))
    a.io.save(state / 'status.json', dict(result={'SC': 0}, delivery_warnings={}))
    process = subprocess.Popen([sys.executable, str(script), str(enrollment), '--watch'], stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            status = a.io.document(state / 'status.json')
            if status.get('error'):
                break
            assert process.poll() is None, 'The watch loop crashed instead of reporting verification failure'
            time.sleep(.05)
        assert 'revalidate custody' in status.get('error', ''), status
        assert status['result'] == {'SC': 0} and status['pending'] is True
        result = a.io.document(state / 'live-result.json')
        assert result['session'] == 'current-session' and result['error'] == status['error']
        assert result['pending'] is True
        assert sentinel.read_bytes() == b'untouched player stash'
        assert pending.read_bytes() == b'{"preserve":"pending transfer"}'
    finally:
        process.terminate()
        process.wait(timeout=10)
print('Agent verification failure is visible in current-session status; saves and recovery journal remain untouched.')
