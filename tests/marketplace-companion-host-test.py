"""Exercise the shipped windowless supervisor on disposable dummy workers only."""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import zipfile

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('host', repo / 'tools/marketplace/companion-host.py')
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


def until(predicate, timeout=12):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        time.sleep(.1)
    raise AssertionError('Supervisor did not reach the expected state')


def read(path):
    try:
        return json.loads(path.read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        return {}


def stop(record):
    if not record or host.process_ticks(record['pid']) != int(record['started_ticks']):
        return
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.TerminateProcess.argtypes = (wintypes.HANDLE, wintypes.UINT)
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    handle = kernel.OpenProcess(1, False, record['pid'])
    if handle:
        try:
            assert kernel.TerminateProcess(handle, 0)
        finally:
            kernel.CloseHandle(handle)


def main():
    assert os.name == 'nt'
    archive = Path(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix='market-companion-host-') as folder:
        root = Path(folder)
        runtime = root / 'runtime' / ('a' * 20)
        runtime.mkdir(parents=True)
        with zipfile.ZipFile(archive) as z:
            z.extractall(runtime)
        # The only code executed by dummy children is sleep. No game paths,
        # credentials, networking, stash parsers or transaction APIs are used.
        dummy = 'import time\nwhile True: time.sleep(.1)\n'
        for name in ('custody-agent.py', 'sync-marketplace.py'):
            (runtime / name).write_text(dummy, encoding='utf-8')
        (runtime / 'companion-update.py').write_text('def check(root, atomic, document): pass\n', encoding='utf-8')
        (runtime / 'companion-host.py').write_bytes((repo / 'tools/marketplace/companion-host.py').read_bytes())
        state = root / 'custody'; state.mkdir()
        saves = root / 'fixture-saves'; saves.mkdir()
        sentinel = saves / 'Stash_SC_Page11.d2i'; sentinel.write_bytes(b'untouched fixture')
        (root / 'pairing.json').write_text(json.dumps({'token':'f' * 64}), encoding='utf-8')
        (root / 'sync-config.json').write_text('{}', encoding='utf-8')
        (root / 'custody-enrollment.json').write_text(json.dumps({'enrolled':True, 'state_directory':str(state), 'save_directory':str(saves), 'managed_runtime':str(runtime)}), encoding='utf-8')
        (root / 'managed-runtime.json').write_text(json.dumps({'runtime':str(runtime), 'profiles':[str(root)]}), encoding='utf-8')
        args = [str(runtime / 'pythonw.exe'), '-B', '-X', 'utf8', str(runtime / 'companion-host.py'), str(root)]
        flags = subprocess.CREATE_NO_WINDOW
        first = subprocess.Popen(args, creationflags=flags)
        second = None
        try:
            until(lambda: host.record_alive(state / 'worker.json') and host.record_alive(root / 'sync-SC.json'))
            original = read(root / 'sync-SC.json')
            second = subprocess.Popen(args, creationflags=flags)
            assert second.wait(8) == 0
            assert read(root / 'sync-SC.json') == original
            stop(original)
            until(lambda: host.record_alive(root / 'sync-SC.json') and read(root / 'sync-SC.json').get('pid') != original['pid'])
            assert first.poll() is None
            assert sentinel.read_bytes() == b'untouched fixture'
            assert read(root / 'companion-status.json')['profiles'][0]['running']
            print('Companion host: bundled runtime, single supervisor, no duplicate workers, automatic worker restart, valid process identity, heartbeat and unchanged fixture save passed.')
        finally:
            first.terminate(); first.wait(8)
            if second is not None and second.poll() is None:
                second.terminate(); second.wait(8)
            stop(read(state / 'worker.json')); stop(read(root / 'sync-SC.json'))
            time.sleep(.4)


if __name__ == '__main__':
    main()
