"""Local-only marketplace UI fixture. Never connects to the production database."""
import argparse
import json
from pathlib import Path
import runpy
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--php', type=Path, required=True)
p.add_argument('--directory', type=Path, required=True)
p.add_argument('--port', type=int, default=8765)
args = p.parse_args()
repo = Path(__file__).resolve().parents[2]
root = args.directory.resolve()
if (root / 'market.sqlite').exists():
    raise SystemExit('Choose a new isolated preview directory.')
fixture = runpy.run_path(str(repo / 'tests/marketplace-web-http-test.py'))['fixture']
env, command, seeded = fixture(args.php.resolve(), root)
(root / 'preview-account.json').write_text(json.dumps({'code':seeded['code']}), encoding='utf-8')
folder = root / 'saves'
folder.mkdir()
(folder / 'Marketplace_Web_Link.json').write_text(json.dumps(dict(schema=1, mod='RMD-MP', game_mode='2.4',
    endpoint=f'http://127.0.0.1:{args.port}/index.php', code=seeded['code'])), encoding='utf-8')
print(f'Local preview: http://127.0.0.1:{args.port}', flush=True)
raise SystemExit(subprocess.call(command + ['-S', f'127.0.0.1:{args.port}', '-t', str(repo / 'tools/marketplace/server/public')], env=env))
