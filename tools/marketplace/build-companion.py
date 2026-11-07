"""Build the private runtime embedded in Pandemonium; never include user state."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile

PYTHON_URL = 'https://www.python.org/ftp/python/3.14.10/python-3.14.10-embed-amd64.zip'
PYTHON_SHA256 = '90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159'
SCRIPTS = ('companion-host.py', 'companion-update.py', 'custody-agent.py', 'custody-workspace.py', 'sync-marketplace.py', 'install-native-layout.py', 'marketplace-http.py')


def build(helper, output, cache, version='0.0.0', hud=None):
    supported = {name: set() for name in ('open_offers', 'torch_class', 'listing_comments', 'gift_resources')}
    # Background updates must keep options supported by earlier HUD builds.
    # Carry only known capability fingerprints, never player/runtime state.
    if output.is_file():
        with zipfile.ZipFile(output) as previous:
            features = json.loads(previous.read('manifest.json')).get('features', {})
            for name, hashes in supported.items():
                for digest in features.get(name + '_hud_sha256', []):
                    if isinstance(digest, str) and len(digest) == 64 and all(c in '0123456789abcdef' for c in digest):
                        hashes.add(digest)
    # Modern HUDs advertise versioned interfaces in their own PE export.
    # --hud is retained for old build callers, but never affects this package.
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / 'python-3.14.10-embed-amd64.zip'
    if not archive.exists():
        with urllib.request.urlopen(PYTHON_URL, timeout=60) as response:
            raw = response.read(20 * 1024 * 1024)
        if hashlib.sha256(raw).hexdigest() != PYTHON_SHA256:
            raise ValueError('Bundled Python checksum mismatch')
        archive.write_bytes(raw)
    raw = archive.read_bytes()
    if hashlib.sha256(raw).hexdigest() != PYTHON_SHA256:
        raise ValueError('Cached Python checksum mismatch')
    entries = {}
    with zipfile.ZipFile(io.BytesIO(raw)) as source:
        for name in source.namelist():
            if name.endswith('/'):
                continue
            if '/' in name or '\\' in name or name.startswith('.'):
                raise ValueError('Unexpected Python archive layout')
            entries[name] = source.read(name)
    # Isolated module lookup: stdlib plus our own bundled scripts only.
    entries['python314._pth'] = b'python314.zip\n.\n'
    for name in SCRIPTS:
        entries[name] = Path(__file__).with_name(name).read_bytes()
        compile(entries[name], name, 'exec')
    entries['custody-save.exe'] = helper.read_bytes()
    for name in ('stash-shop-button.json', 'marketplace-workspace.json', 'stash-sync-mask.json'):
        entries[name] = Path(__file__).with_name('layouts').joinpath(name).read_bytes()
    for name in ('MarketplaceButton.sprite', 'MarketplaceButton.lowend.sprite', 'MarketplaceGift.sprite', 'MarketplaceGift.lowend.sprite'):
        entries[name] = Path(__file__).with_name('assets').joinpath(name).read_bytes()
    if not entries['custody-save.exe'].startswith(b'MZ'):
        raise ValueError('Build the Windows custody helper before packaging')
    inputs = [Path(__file__), *Path(__file__).parent.glob('*.h'),
              *Path(__file__).resolve().parents[2].joinpath('d2rhud/plugin').rglob('*.h'),
              *[Path(__file__).with_name(name) for name in SCRIPTS],
              *Path(__file__).with_name('layouts').glob('*.json'), *Path(__file__).with_name('assets').glob('*.sprite'), Path(__file__).with_name('custody-save.cpp')]
    # An internal build timestamp allows fixes without changing the app version.
    # Stable input mtimes retain repeatable packaging of the same build.
    build_epoch = max(p.stat().st_mtime_ns // 1_000_000 for p in inputs)
    manifest = {'version': version, 'build_epoch': build_epoch, 'hud_protocol': 1, 'features': {name+'_hud_sha256': sorted(hashes) for name,hashes in supported.items()}, 'schema': 1, 'platform': 'win-x64', 'python': '3.14.10',
                'files': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(entries.items())}}
    entries['manifest.json'] = (json.dumps(manifest, sort_keys=True, indent=2) + '\n').encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    # Fixed timestamps make identical sources produce identical payloads.
    with zipfile.ZipFile(output.with_suffix('.new'), 'w', zipfile.ZIP_DEFLATED) as target:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            target.writestr(info, data)
    output.with_suffix('.new').replace(output)
    print(json.dumps({'payload': str(output), 'files': len(entries),
                      'sha256': hashlib.sha256(output.read_bytes()).hexdigest()}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--helper', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--version', default='0.0.0')
    p.add_argument('--hud', type=Path)
    a = p.parse_args()
    build(a.helper, a.output, a.cache, a.version, a.hud)
