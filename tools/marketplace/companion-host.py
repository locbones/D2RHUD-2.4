"""Windowless, per-user supervisor shipped inside Pandemonium. No global Python needed."""
import argparse
import ctypes
import json
import os
import runpy
from pathlib import Path
import subprocess
import threading
import time


def document(path):
    with Path(path).open('r', encoding='utf-8-sig') as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError('Marketplace configuration must be an object')
    return value


def atomic(path, value):
    temporary = Path(str(path) + '.new')
    temporary.write_text(json.dumps(value, ensure_ascii=False) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def process_ticks(pid):
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.GetProcessTimes.argtypes = (wintypes.HANDLE,) + (ctypes.POINTER(wintypes.FILETIME),) * 4
    kernel.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    handle = kernel.OpenProcess(0x1000, False, int(pid))
    if not handle:
        return None
    try:
        exit_code = wintypes.DWORD()
        if not kernel.GetExitCodeProcess(handle, ctypes.byref(exit_code)) or exit_code.value != 259:
            return None
        values = [wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(handle, *(ctypes.byref(v) for v in values)):
            return None
        return (values[0].dwHighDateTime << 32) + values[0].dwLowDateTime + 504911232000000000
    finally:
        kernel.CloseHandle(handle)


def record_alive(path):
    try:
        record = document(path)
        return process_ticks(record['pid']) == int(record['started_ticks'])
    except (OSError, ValueError, KeyError, TypeError):
        return False


_log_lock = threading.Lock()


def log_line(path, line, source='supervisor'):
    # One bounded stream for all workers, tagged by mod and subsystem.
    # Rotation and appends are serialized across drain/update threads.
    try:
        with _log_lock:
            if path.exists() and path.stat().st_size > 1024 * 1024:
                os.replace(path, path.with_suffix('.previous.log'))
            with path.open('a', encoding='utf-8') as stream:
                stamp = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
                for text in line.splitlines():
                    stream.write(f'{stamp} [{source}] {text}\n')
    except OSError:
        pass


def drain(stream, path, source):
    try:
        for line in stream:
            log_line(path, line, source)
    finally:
        stream.close()


def launch(config, profile, mode, runtime, diagnostics_root):
    root = Path(config['state_directory'])
    if mode == 'custody':
        script = runtime / 'custody-agent.py'
        arguments = [str(script), str(profile / 'custody-enrollment.json'), '--watch']
        record = root / 'worker.json'
        log = diagnostics_root / 'diagnostics.log'
    else:
        script = runtime / 'sync-marketplace.py'
        arguments = [str(script), str(profile / 'sync-config.json'), config['save_directory'], '--watch']
        if mode == 'HC':
            arguments.append('--hardcore')
        record = profile / ('sync-' + mode + '.json')
        log = diagnostics_root / 'diagnostics.log'
    if record_alive(record):
        return None
    env = dict(os.environ)
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    env['PYTHONUTF8'] = '1'
    env['D2_MARKET_TOKEN'] = document(profile / 'pairing.json')['token']
    child = subprocess.Popen([str(runtime / 'python.exe'), '-B', '-X', 'utf8', *arguments],
                             env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace',
                             creationflags=subprocess.CREATE_NO_WINDOW)
    ticks = process_ticks(child.pid)
    atomic(record, {'pid': child.pid, 'started_ticks': str(ticks), 'script': str(script),
                    'entrypoint': str(script), 'mode': mode, 'managed': True})
    threading.Thread(target=drain, args=(child.stdout, log, config.get('mod', 'RMD-MP') + '/' + mode), daemon=True).start()
    return child


def supervise_profile(root, profile_name, runtime, children):
    profile = Path(profile_name).resolve()
    if profile != root and root not in profile.parents:
        raise ValueError('Invalid managed marketplace profile')
    config = document(profile / 'custody-enrollment.json')
    if config.get('enrolled') is not True:
        return None
    profile_runtime = Path(config.get('managed_runtime', str(runtime))).resolve()
    if root / 'runtime' not in profile_runtime.parents:
        raise ValueError('Invalid managed marketplace runtime')
    modes = ['custody', 'SC']
    if (Path(config['save_directory']) / 'Marketplace_2.4_HC.json').exists():
        modes.append('HC')
    for mode in modes:
        key = (str(profile), mode)
        child = children.get(key)
        if child is None or child.poll() is not None:
            child = launch(config, profile, mode, profile_runtime, root)
            if child is not None:
                children[key] = child
    return {'mod': config.get('mod', 'RMD-MP'), 'profile': str(profile), 'running': True}


def run(root, once=False):
    root = Path(root).resolve()
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                   wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    handle = None
    for attempt in range(25):
        handle = kernel.CreateFileW(str(root / 'managed-host.lock'), 0xC0000000, 0, None, 4, 0x80, None)
        if handle != ctypes.c_void_p(-1).value:
            break
        try:
            previous = document(root / 'managed-host.json')
            if previous.get('runtime') == str(Path(__file__).resolve().parent):
                return 0
        except (OSError, ValueError):
            return 0
        time.sleep(.2)
    if handle == ctypes.c_void_p(-1).value:
        return 0  # Another supervisor is already running. Never duplicate workers.
    children = {}
    update_thread = None
    update_checked = 0.0
    def update():
        try:
            updater = runpy.run_path(str(Path(__file__).with_name('companion-update.py')))
            updater['check'](root, atomic, document)
        except Exception as error:
            log_line(root / 'diagnostics.log', 'Companion update deferred: ' + str(error)[:256] + '\n')
    try:
        atomic(root / 'managed-host.json', {'pid': os.getpid(), 'started_ticks': str(process_ticks(os.getpid())), 'runtime': str(Path(__file__).resolve().parent)})
        while True:
            # Network and signature work never stalls supervision or game input.
            # At most one check every five minutes; writers retain their leases.
            if not once and time.monotonic() - update_checked >= 300 and (update_thread is None or not update_thread.is_alive()):
                update_checked = time.monotonic()
                update_thread = threading.Thread(target=update, daemon=True)
                update_thread.start()
            status = []
            try:
                settings = document(root / 'managed-runtime.json')
                runtime = Path(settings['runtime']).resolve()
                if runtime != Path(__file__).resolve().parent:
                    return 0  # Launcher has staged a newer supervisor.
                for profile_name in settings['profiles']:
                    try:
                        row = supervise_profile(root, profile_name, runtime, children)
                        if row is not None: status.append(row)
                    except (OSError, ValueError, KeyError, TypeError) as error:
                        # One interrupted profile must not starve every other
                        # mod, nor require restarting the supervisor to recover.
                        log_line(root / 'diagnostics.log', str(error)[:512], 'profile')
                        status.append({'profile': str(profile_name), 'running': False, 'error': str(error)[:512]})
                atomic(root / 'companion-status.json', {'schema': 1, 'at': int(time.time()), 'profiles': status})
            except (OSError, ValueError, KeyError) as error:
                log_line(root / 'diagnostics.log', str(error)[:512] + '\n')
            if once:
                return 0
            time.sleep(2)
    finally:
        # Workers finish their current operation before adopting staged code.
        # Never terminate a save writer merely because the supervisor is updated.
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--once', action='store_true')
    args = parser.parse_args()
    try:
        raise SystemExit(run(args.root, args.once))
    except Exception as error:
        log_line(args.root / 'diagnostics.log', type(error).__name__ + ': ' + str(error)[:512] + '\n')
        raise SystemExit(1)
