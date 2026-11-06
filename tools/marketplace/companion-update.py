"""Signed, windowless runtime upgrades; never write a stash or interrupt a writer."""
import base64
from contextlib import ExitStack
import hashlib
import io
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import tempfile
import time
import urllib.request
import zipfile

ORIGIN = 'https://market.d2rmodding.com/companion/'
PUBLIC_MODULUS = '0ZJA78q6pZ90YP7HCiT5V7N8HY9p3Aj7sDg3ImS0iesKDrFD59gEVni2LKwqcWcAErzUrISyLYpobNE9hBZcNi7BiICO4x7yFRFKoVSWI27gw1kblachk4e1AtFo/IRqqGE+VbV8KXtvs18yecghruozmk2mRvImMMIK0J9XGwmEgqrfsmJcLa0x2EWy3Kz9zKCE3m13uwMDGD/DFVZhHER82Ok0wG33viT7Gtk8hh7t3naFoWHqkmLZUPNzqSqc5DltlWCk+anQqfFWb1VXYCgx2ONQCqttGpAPQOstCkElvXvf4H4P1vNsvR/ci5UGhZwrhizDjmmi/mmwqgjJWVgaCLkErrlvzWKMTI0XvSfIFr2MVQ26FywaBVP2LRQb4KUjXglOr7EvzWeWbZnk6vhRKVa+90YlrKOofK2xSx/cH+Z/IGt0ildiYPL3KLH2mmH9Ij4yOyLBasFFRJVbZJtdY+/Dkh6nQnVFuaRq3S92D7PQeuJXCJizaQ+B/0Yp'
MAX_PACKAGE = 40 * 1024 * 1024


def verify_envelope(raw, modulus=PUBLIC_MODULUS):
    if len(raw) > 32768:
        raise ValueError('Update manifest is too large')
    envelope = json.loads(raw)
    payload = base64.b64decode(envelope['payload'], validate=True)
    signature = base64.b64decode(envelope['signature'], validate=True)
    key = int.from_bytes(base64.b64decode(modulus, validate=True), 'big')
    size = (key.bit_length() + 7) // 8
    if len(signature) != size or int.from_bytes(signature, 'big') >= key:
        raise ValueError('Invalid update signature')
    digest = bytes.fromhex('3031300d060960864801650304020105000420') + hashlib.sha256(payload).digest()
    expected = b'\x00\x01' + b'\xff' * (size - len(digest) - 3) + b'\x00' + digest
    actual = pow(int.from_bytes(signature, 'big'), 65537, key).to_bytes(size, 'big')
    if not __import__('hmac').compare_digest(expected, actual):
        raise ValueError('Invalid update signature')
    manifest = json.loads(payload)
    if manifest.get('schema') != 1 or manifest.get('platform') != 'win-x64' or manifest.get('protocol') != 1:
        raise ValueError('Unsupported companion update')
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', str(manifest.get('version', ''))):
        raise ValueError('Invalid update version')
    sha = manifest.get('sha256', '')
    if not re.fullmatch('[0-9a-f]{64}', sha) or manifest.get('file') != sha + '.zip':
        raise ValueError('Invalid update package identity')
    if type(manifest.get('sequence')) is not int or manifest['sequence'] < 1:
        raise ValueError('Invalid update sequence')
    if type(manifest.get('size')) is not int or not 1 <= manifest['size'] <= MAX_PACKAGE:
        raise ValueError('Invalid update size')
    return manifest


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError('Companion updates must remain on their trusted origin')


def download(name, limit):
    # No player credential, save data or account identity leaves the machine.
    with urllib.request.build_opener(NoRedirect()).open(ORIGIN + name, timeout=20) as response:
        raw = response.read(limit + 1)
    if len(raw) > limit:
        raise ValueError('Update download exceeds its size limit')
    return raw


def unpack(raw, parent, sha):
    destination = parent / sha[:20]
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        names = archive.namelist()
        if len(set(names)) != len(names) or len(names) > 100:
            raise ValueError('Invalid update archive')
        if any(Path(n).name != n or '/' in n or '\\' in n or ':' in n or n.startswith('.') for n in names):
            raise ValueError('Invalid runtime file path')
        if sum(entry.file_size for entry in archive.infolist()) > 256 * 1024 * 1024:
            raise ValueError('Runtime exceeds extraction limit')
        manifest = json.loads(archive.read('manifest.json'))
        required = {'python.exe', 'pythonw.exe', 'custody-save.exe', 'custody-agent.py', 'custody-workspace.py', 'sync-marketplace.py', 'companion-host.py', 'companion-update.py', 'install-native-layout.py'}
        if not required <= set(manifest['files']) or set(names) != set(manifest['files']) | {'manifest.json'}:
            raise ValueError('Incomplete companion update')
        def valid(path):
            return path.is_dir() and not path.is_symlink() and all((path / name).is_file() and not (path / name).is_symlink() and hashlib.sha256((path / name).read_bytes()).hexdigest() == digest for name, digest in manifest['files'].items())
        if valid(destination):
            return destination
        if destination.exists():
            raise ValueError('An existing runtime failed verification; use Start / repair')
        parent.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix='.update-', dir=parent))
        try:
            for name in names:
                value = archive.read(name)
                if name != 'manifest.json' and hashlib.sha256(value).hexdigest() != manifest['files'][name]:
                    raise ValueError('Companion file checksum mismatch')
                (stage / name).write_bytes(value)
            if not valid(stage):
                raise ValueError('Incomplete extracted runtime')
            stage.rename(destination)
        finally:
            if stage.exists():
                shutil.rmtree(stage)
    return destination


def apply(root, runtime, lease, atomic, document):
    with lease(root / 'setup.lock'):
        managed = document(root / 'managed-runtime.json')
        complete = True
        helper_hash = hashlib.sha256((runtime / 'custody-save.exe').read_bytes()).hexdigest()
        for profile_name in managed.get('profiles', []):
            profile = Path(profile_name).resolve()
            if profile != root and root not in profile.parents:
                raise ValueError('Invalid update profile')
            config_path = profile / 'custody-enrollment.json'
            config = document(config_path)
            state = Path(config['state_directory']).resolve()
            if root not in state.parents:
                raise ValueError('Invalid recovery directory')
            try:
                with lease(state / 'guard.lock'):
                    config = document(config_path)
                    if (state / 'pending.json').exists():
                        complete = False
                        continue
                    if Path(config.get('managed_runtime', '')).resolve() != runtime:
                        # Stream explicitly: Python 3.14 copy2 uses CopyFile2,
                        # which older supported Wine builds do not implement.
                        with config_path.open('rb') as source, (profile / 'custody-enrollment.previous.json').open('wb') as backup:
                            shutil.copyfileobj(source, backup)
                        # Existing owner, token paths, mod, catalog and guarded
                        # launcher/HUD hashes stay exactly as enrolled.
                        config.update(managed_runtime=str(runtime), python=str(runtime / 'python.exe'),
                                      agent_script=str(runtime / 'custody-agent.py'), helper=str(runtime / 'custody-save.exe'), helper_sha256=helper_hash)
                        atomic(config_path, config)
                    sync_path = profile / 'sync-config.json'
                    sync = document(sync_path)
                    if sync.get('managed_runtime') != str(runtime):
                        sync['managed_runtime'] = str(runtime)
                        atomic(sync_path, sync)
            except OSError:
                complete = False  # A writer owns the lease. Retry after it finishes.
        if not complete:
            return False
        managed['runtime'] = str(runtime)
        atomic(root / 'managed-runtime.json', managed)
        if os.name == 'nt':
            import winreg
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r'Software\Microsoft\Windows\CurrentVersion\Run') as key:
                winreg.SetValueEx(key, 'Pandemonium Marketplace', 0, winreg.REG_SZ, f'"{runtime / "pythonw.exe"}" -B -X utf8 "{runtime / "companion-host.py"}" "{root}"')
        return True


def check(root, atomic, document):
    root = Path(root).resolve()
    current = Path(__file__).resolve().parent
    envelope = download('manifest.json', 32768)
    manifest = verify_envelope(envelope)
    local = document(current / 'manifest.json')
    if tuple(map(int, manifest['version'].split('.'))) < tuple(map(int, local.get('version', '0.0.0').split('.'))):
        return
    cache = root / 'updates'
    cache.mkdir(exist_ok=True)
    previous = cache / 'manifest.json'
    if previous.exists() and verify_envelope(previous.read_bytes())['sequence'] > manifest['sequence']:
        raise ValueError('Companion rollback refused')
    if current.name == manifest['sha256'][:20]:
        # The launcher may have upgraded only the selected mod. Converge all
        # enrolled profiles even when this supervisor is already current.
        support = runpy.run_path(str(current / 'custody-agent.py'))
        apply(root, current, support['lease'], atomic, document)
        return
    package = cache / 'companion.zip'
    if package.exists() and hashlib.sha256(package.read_bytes()).hexdigest() == manifest['sha256']:
        raw = package.read_bytes()
    else:
        raw = download(manifest['file'], manifest['size'])
    if len(raw) != manifest['size'] or hashlib.sha256(raw).hexdigest() != manifest['sha256']:
        raise ValueError('Companion package checksum mismatch')
    runtime = unpack(raw, root / 'runtime', manifest['sha256'])
    candidate = document(runtime / 'manifest.json')
    if candidate.get('version') != manifest['version']:
        raise ValueError('Companion package version differs from its release manifest')
    if candidate.get('version') == local.get('version') and candidate.get('build_epoch', 0) < local.get('build_epoch', 0):
        return  # A cached channel must not undo a newer same-version launcher build.
    support = runpy.run_path(str(current / 'custody-agent.py'))
    # Commit the authenticated cache under the same lease used by the launcher.
    # No network is performed while holding any setup or save lease.
    with support['lease'](root / 'setup.lock'):
        temporary = cache / 'companion.new'
        temporary.write_bytes(raw)
        os.replace(temporary, package)
        temporary = cache / 'manifest.new'
        temporary.write_bytes(envelope)
        os.replace(temporary, previous)
    if apply(root, runtime, support['lease'], atomic, document):
        subprocess.Popen([str(runtime / 'pythonw.exe'), '-B', '-X', 'utf8', str(runtime / 'companion-host.py'), str(root)],
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
