"""Real Windows leases/process identity, with disposable live-handoff files."""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('live_agent', repo / 'tools/marketplace/custody-agent.py')
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


def shared(path):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = (ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                       ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p)
    create.restype = ctypes.c_void_p
    handle = create(str(path), 0x80000000, 1, None, 4, 0x80, None)
    assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
    return handle


def close(handle):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CloseHandle.argtypes = (ctypes.c_void_p,)
    kernel.CloseHandle(handle)


def main():
    assert os.name == 'nt'
    with tempfile.TemporaryDirectory(prefix='market-live-agent-') as temporary:
        root = Path(temporary)
        game, saves, state = [root / p for p in ('game', 'saves', 'state')]
        data = game / 'Mods/RMD-MP/RMD-MP.mpq/data'
        for path in (game, saves, state, data / 'global/excel'):
            path.mkdir(parents=True)
        for name in ('weapons', 'armor', 'misc', 'itemstatcost', 'itemtypes'):
            (data / f'global/excel/{name}.bin').write_bytes(b'fixture')
        fake = game / 'D2R.exe'
        shutil.copy2(Path(os.environ['SystemRoot']) / 'System32/ping.exe', fake)
        helper, launcher, hud = root / 'helper.exe', root / 'launcher.exe', game / 'd2rhud.dll'
        for path in (helper, launcher, hud):
            path.write_bytes(path.name.encode())
        child = subprocess.Popen([str(fake), '-n', '60', '127.0.0.1'], stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
        handles = []
        try:
            _, started = a.live_process(child.pid)
            # Isolate these checks from a real game the developer may have open.
            a.game_processes = lambda: {child.pid}
            cfg = dict(schema=1, enrolled=True, owner='a' * 32, state_directory=str(state), save_directory=str(saves),
                       mod_data=str(data), catalog=a.catalog(data), game_directory=str(game), helper=str(helper),
                       helper_sha256=a.io.digest(helper.read_bytes()), launcher_path=str(launcher),
                       launcher_sha256=a.io.digest(launcher.read_bytes()), hud_sha256=a.io.digest(hud.read_bytes()))
            settings = dict(schema=1, game_mode='2.4', mod='RMD-MP', hardcore=False, page=11, withdraw_page=2)
            a.io.save(saves / 'Marketplace_2.4_SC.json', settings)
            window = dict(schema=1, session='fixture-session-1', phase='parked', pid=child.pid, started=started,
                          tick=a.ticks(), owner=cfg['owner'], catalog=cfg['catalog'], player=17, hardcore=False,
                          deposit=11, delivery=2, parking=1, loaded=1, save_directory=str(saves))
            path = state / 'live-window.json'
            a.io.save(path, window)
            agent = a.Agent(cfg)
            with agent.save_access('SC', [11, 2]) as granted:
                assert granted['session'] == window['session']
            def refuses(mode='SC', pages=(11,)):
                try:
                    with agent.save_access(mode, pages):
                        raise AssertionError('An invalid live window authorized access')
                except BlockingIOError:
                    pass
            for patch in (dict(tick=0), dict(started=started+1), dict(owner='b'*32), dict(catalog='f'*64),
                          dict(loaded=11), dict(parking=2), dict(save_directory=str(root)), dict(phase='loaded')):
                a.io.save(path, dict(window, **patch))
                refuses()
            a.io.save(path, window)
            refuses('HC')
            refuses(pages=(3,))
            per_page = dict(window, schema=2, phase='unloaded', loaded=11)
            a.io.save(path, per_page)
            with agent.save_access('SC', [2]):
                pass
            with agent.save_access('SC', []):
                pass  # Queue polling and sidecars do not touch a loaded page.
            refuses(pages=(11,))
            refuses(pages=(11, 2))
            refuses(pages=None)
            a.io.save(path, dict(per_page, loaded=2))
            with agent.save_access('SC', [11]):
                pass
            refuses(pages=(2,))
            a.io.save(path, window)
            a.game_processes = lambda: {child.pid, child.pid+1}
            refuses()
            a.game_processes = lambda: {child.pid}
            handle = shared(state / 'live.lock')
            refuses()
            close(handle)
            # A reopen during cloud authorization cannot race the local write.
            # Pending work survives and resumes through a fresh unloaded window.
            page = saves / 'Stash_SC_Page11.d2i'
            before, after = b'original complete page', b'authorized complete page'
            page.write_bytes(before)
            intercept = True
            def transport(mode, packet):
                if packet['type'] == 'write_authorize' and intercept:
                    handles.append(shared(state / 'live.lock'))
                return dict(kind='deposit', status='prepared')
            agent.transport = transport
            try:
                with a.lease(state / 'guard.lock'):
                    agent.start('SC', dict(id='1'*32, type='deposit_prepare', items=[]), [(11, before, after)])
                raise AssertionError('A reopened trade page allowed a native write')
            except BlockingIOError:
                assert agent.pending.exists() and page.read_bytes() == before
            close(handles.pop())
            intercept = False
            with a.lease(state / 'guard.lock'):
                agent.recover()
            assert not agent.pending.exists() and page.read_bytes() == after
            # Wake notifications eliminate the old 15-second close/reopen race.
            event = a.WakeEvent(cfg['owner'])
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.SetEvent.argtypes = (ctypes.c_void_p,)
            kernel.SetEvent(event.handle)
            start = time.monotonic()
            event.wait(15)
            assert time.monotonic() - start < 1
        finally:
            for handle in handles:
                close(handle)
            child.terminate()
            child.wait(timeout=5)
    print('Live agent: native PID/start identity, owner/catalog/routes/mode/heartbeat validation, multibox refusal, live-write exclusion, deferred recovery and immediate wake passed.')


if __name__ == '__main__':
    main()
