"""Gameplay can start during cloud work; only atomic local saves take the gate."""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('agent', repo / 'tools/marketplace/custody-agent.py')
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


def shared_game(path):
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
    assert kernel.CloseHandle(handle)


def main():
    if os.name != 'nt':
        raise SystemExit('Windows sharing-mode test requires Windows')
    with tempfile.TemporaryDirectory(prefix='market-game-gate-') as temporary:
        root = Path(temporary)
        # Exercise the actual process snapshot with an owned, harmless Windows
        # ping process bearing the game's image name, then terminate only it.
        fake_game = root / 'D2R.exe'
        shutil.copy2(Path(os.environ['SystemRoot']) / 'System32/ping.exe', fake_game)
        child = subprocess.Popen([str(fake_game), '-n', '30', '127.0.0.1'],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            assert a.game_running(), 'Local process snapshot missed the game image'
        finally:
            child.terminate()
            child.wait(timeout=5)
        saves = root / 'saves'
        saves.mkdir()
        (saves / 'custody-simulation.json').write_text('{}', encoding='utf-8')
        data = root / 'mods' / 'RMD-MP' / 'RMD-MP.mpq' / 'data'
        (data / 'global/excel').mkdir(parents=True)
        for name in ('weapons', 'armor', 'misc', 'itemstatcost', 'itemtypes'):
            (data / f'global/excel/{name}.bin').write_bytes(b'fixture')
        helper = root / 'helper.exe'
        helper.write_bytes(b'fixture helper')
        cfg = dict(simulation=True, state_directory=str(root / 'state'), save_directory=str(saves),
                   mod_data=str(data), helper=str(helper), helper_sha256=a.io.digest(helper.read_bytes()), catalog=a.catalog(data))
        handles = []
        launch = True
        change_save = False
        def transport(mode, packet):
            if packet['type'] == 'write_authorize':
                if launch:
                    # This is a genuine Windows shared lease, acquired while
                    # cloud work still owns the companion transaction lock.
                    handles.append(shared_game(agent.game_lease))
                if change_save:
                    page.write_bytes(b'player changed this page')
            return dict(kind='deposit', status='prepared')
        agent = a.Agent(cfg, transport)
        page = agent.page('SC', 11)
        before, after = b'original complete page', b'authorized complete page'
        page.write_bytes(before)
        try:
            with a.lease(agent.root / 'guard.lock'):
                agent.start('SC', dict(id='1' * 32, type='deposit_prepare', items=[]), [(11, before, after)])
            raise AssertionError('A running game permitted a save write')
        except OSError:
            assert agent.pending.exists() and page.read_bytes() == before
        close(handles.pop())
        launch = False
        with a.lease(agent.root / 'guard.lock'):
            agent.recover()
        assert page.read_bytes() == after and not agent.pending.exists()
        sidecar = saves / 'settings.json'
        a.io.save(sidecar, dict(price=1))
        handle = shared_game(agent.game_lease)
        try:
            try:
                agent.save_sidecar(sidecar, dict(price=2), dict(price=1))
                raise AssertionError('Running game permitted a settings update')
            except OSError:
                assert a.io.document(sidecar) == dict(price=1)
        finally:
            close(handle)
        # A page changed during authorization is preserved, even once gameplay
        # has released its lease. Only marketplace reconciliation is paused.
        page.write_bytes(before)
        change_save = True
        try:
            with a.lease(agent.root / 'guard.lock'):
                agent.start('SC', dict(id='2' * 32, type='deposit_prepare', items=[]), [(11, before, after)])
            raise AssertionError('A modified player save was overwritten')
        except ValueError as error:
            assert 'reconciliation' in str(error)
            assert page.read_bytes() == b'player changed this page'
        # The process check remains a fallback if the launcher/HUD cannot open
        # its gate. Never depend on successful marketplace initialization.
        agent.test = False
        a.game_running = lambda: True
        try:
            with agent.save_access():
                raise AssertionError('Running-game fallback allowed writes')
        except BlockingIOError:
            pass
    print('Gameplay gate: cloud-start race, deferred journal recovery, sidecar exclusion, changed-save preservation and running-process fallback passed.')


if __name__ == '__main__':
    main()
