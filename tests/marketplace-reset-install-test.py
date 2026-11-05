"""Test backup-before-delete and pending-transfer refusal on a disposable profile."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
script = repo / 'tools/marketplace/reset-development-install.ps1'
with tempfile.TemporaryDirectory(prefix='market-reset-install-') as temporary:
    root = Path(temporary)
    (root / 'marketplace-reset-fixture.json').write_text('{}', encoding='utf-8')
    profile = root / 'profile'
    state = profile / 'custody'
    state.mkdir(parents=True)
    saves = root / 'saves'
    saves.mkdir()
    game = root / 'game'
    game.mkdir()
    (profile / 'pairing.json').write_text('{"token":"fixture only"}', encoding='utf-8')
    (profile / 'custody-enrollment.json').write_text(json.dumps({'state_directory':str(state),'save_directory':str(saves),'game_directory':str(game)}), encoding='utf-8')
    (saves / 'Marketplace_2.4_SC.json').write_text('{"page":11}', encoding='utf-8')
    sentinel = saves / 'Stash_SC_Page11.d2i'
    sentinel.write_bytes(b'unchanged native fixture')
    env = dict(os.environ, D2_MARKET_RESET_TEST='1')
    base = ['pwsh', '-NoLogo', '-NoProfile', '-File', str(script), '-TestRoot', str(root)]
    pending = state / 'pending.json'
    pending.write_text('{"pending":"fixture"}', encoding='utf-8')
    blocked = subprocess.run(base + ['-BackupDirectory',str(root / 'backups/blocked'),'-Apply'],env=env,capture_output=True,text=True)
    assert blocked.returncode != 0 and profile.exists() and pending.exists()
    pending.unlink()
    backup = root / 'backups/verified'
    result = subprocess.run(base + ['-BackupDirectory',str(backup),'-Apply'],env=env,capture_output=True,text=True)
    assert result.returncode == 0, result.stderr
    assert not profile.exists() and not (saves / 'Marketplace_2.4_SC.json').exists()
    assert sentinel.read_bytes() == b'unchanged native fixture'
    assert (backup / 'profile/pairing.json').read_text(encoding='utf-8') == '{"token":"fixture only"}'
    report = json.loads((backup / 'reset-result.json').read_text(encoding='utf-8-sig'))
    assert report['native_saves_removed'] == 0 and report['verified_files'] >= 4
    assert json.loads((backup / 'manifest.json').read_text(encoding='utf-8-sig'))
    print('Clean-install reset: pending-transfer refusal, full verified private profile/sidecar backup, active setup removal and unchanged native stash passed.')
