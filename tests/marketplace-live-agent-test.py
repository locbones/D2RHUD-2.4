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
        with (Path(os.environ['SystemRoot']) / 'System32/ping.exe').open('rb') as source, fake.open('wb') as target:
            shutil.copyfileobj(source, target)
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
            settings = dict(schema=1, game_mode='2.4', mod='RMD-MP', hardcore=False, page=11, withdraw_page=2, drafts=[])
            a.io.save(saves / 'Marketplace_2.4_SC.json', settings)
            window = dict(schema=1, session='fixture-session-1', phase='parked', pid=child.pid, started=started,
                          tick=a.ticks(), owner=cfg['owner'], catalog=cfg['catalog'], player=17, hardcore=False,
                          deposit=11, delivery=2, parking=1, loaded=1, save_directory=str(saves))
            path = state / 'live-window.json'
            a.io.save(path, window)
            agent = a.Agent(cfg)
            # Late first-run setup is independent of the game/live-save leases.
            # Only disabled preferences and capability metadata are initialized.
            fresh_saves=root/'first-saves';fresh_saves.mkdir()
            first_state=root/'first-state';first_state.mkdir()
            fresh=a.Agent(dict(cfg,save_directory=str(fresh_saves),state_directory=str(first_state)))
            unchanged=fresh_saves/'Stash_SC_Page1.d2i';unchanged.write_bytes(b'never edit at bootstrap')
            game_lease=shared(first_state/'game.lock');live_lease=shared(first_state/'live.lock')
            try:
                prefs=fresh_saves/'Marketplace_2.4_SC.json'
                with a.lease(str(prefs)+'.lock'):
                    try: fresh.bootstrap();raise AssertionError('Bootstrap bypassed settings writer')
                    except OSError: pass
                fresh.bootstrap()
                defaults=a.io.document(prefs)
                assert defaults['enabled'] is False and defaults['auto_trade'] is False
                assert defaults['drafts']==[] and defaults['page']==defaults['withdraw_page']==1
                before=prefs.read_bytes();stamp=(first_state/'live-capability.json').stat().st_mtime_ns
                fresh.bootstrap();assert prefs.read_bytes()==before
                assert (first_state/'live-capability.json').stat().st_mtime_ns==stamp
                (first_state/'live-capability.json').write_text('interrupted derived metadata',encoding='utf-8')
                fresh.bootstrap();assert a.io.document(first_state/'live-capability.json')['owner']==cfg['owner']
                changed=dict(defaults,enabled=True,page=20,withdraw_page=20,drafts=[{'preserved':'price'}])
                a.io.save(prefs,changed);fresh.bootstrap();assert a.io.document(prefs)==changed
                prefs.unlink();a.io.save(first_state/'pending.json',{'preserved':'journal'})
                fresh.bootstrap();assert not prefs.exists()
                a.io.save(first_state/'status.json',{'error':'real recovery error','pending':True})
                a.report_waiting(fresh.config);assert a.io.document(first_state/'status.json')['error']=='real recovery error'
                (first_state/'pending.json').unlink();a.report_waiting(fresh.config)
                assert a.io.document(first_state/'status.json')['error']==''
                assert unchanged.read_bytes()==b'never edit at bootstrap'
            finally:
                close(game_lease);close(live_lease)
            # Continue the real Windows lease/process tests with a modern HUD
            # whose whole-file hash differs from its enrolled legacy build.
            protocol_spec = importlib.util.spec_from_file_location('live_protocol_fixture', repo / 'tests/marketplace-hud-protocol-test.py')
            protocol_fixture = importlib.util.module_from_spec(protocol_spec)
            protocol_spec.loader.exec_module(protocol_fixture)
            hud.write_bytes(protocol_fixture.pe())
            assert a.io.digest(hud.read_bytes()) != cfg['hud_sha256']
            agent.verify()
            assert a.io.hud_features(cfg, {}) == list(a.io.HUD_FEATURES)
            # A running transfer releases the repair scheduling gate while
            # retaining its actual transfer lock. Repair can queue fairly.
            with agent.guard_access():
                with a.lease(state / 'repair.lock'):
                    try:
                        with a.lease(state / 'guard.lock'):
                            raise AssertionError('A running transfer lost its guard')
                    except OSError:
                        pass
            with a.lease(state / 'repair.lock'):
                try:
                    agent.run()
                    raise AssertionError('A sync cycle started during repair')
                except a.RepairInProgress:
                    pass
                assert not agent.pending.exists()
                with a.lease(state / 'guard.lock'):
                    pass
            with agent.guard_access():
                pass  # Closing/cancelling repair resumes the existing worker.
            with agent.save_access('SC', [11, 2]) as granted:
                assert granted['session'] == window['session']
            # Stale HUD routes must never authorize access, even for sidecars.
            a.io.save(path, dict(window, deposit=1, delivery=1, parking=2, loaded=11))
            try:
                with agent.save_access('SC', []):
                    raise AssertionError('Stale market routes authorized a save')
            except BlockingIOError as error:
                assert 'older market page settings' in str(error) and 'Start / repair' in str(error)
            a.io.save(path, window)
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
            # Polling, clicks and deposit checks never request visible reloads.
            viewed = dict(per_page, refresh_supported=2, loaded=2, refresh_id='')
            a.io.save(path, viewed)
            for reason in ('deposit', 'browse', 'withdraw:', 'items:invalid'):
                assert not agent.request_refresh('SC', 2, reason)
            reason = 'withdraw:' + 'c' * 32
            assert not agent.request_refresh('SC', 11, reason)
            assert agent.request_refresh('SC', 2, reason)
            request = a.io.document(state / 'live-refresh-request.json')
            assert request['kind'] == 'delivery' and request['page'] == 2
            assert agent.refresh_active()  # Skip optional uploads before native parking starts.
            a.io.save(state / 'live-refresh-request.json', dict(request, tick='corrupt'))
            assert not agent.refresh_active()
            a.io.save(state / 'live-refresh-request.json', request)
            assert agent.request_refresh('SC', 2, reason)
            assert a.io.document(state / 'live-refresh-request.json')['id'] == request['id']
            refuses(pages=(2,))
            refreshed = dict(viewed, loaded=1, session='parked-refresh-session', refresh_id=request['id'])
            a.io.save(path, refreshed)
            assert agent.refresh_active()
            agent.refresh_ready(refreshed)
            result = a.io.document(state / 'live-refresh-result.json')
            assert result == dict(schema=1, id=request['id'], session=refreshed['session'], ready=True)
            # Neither time nor a new native grant re-arms the same delivery.
            seen_path = state / 'refresh-seen.json'
            seen = a.io.document(seen_path)
            for value in seen.values(): value['at'] = 0
            a.io.save(seen_path, seen)
            a.io.save(path, dict(viewed, session='returned-session', edit_revision=999))
            for _ in range(20): assert not agent.request_refresh('SC', 2, reason)
            assert agent.request_refresh('SC', 2, 'withdraw:' + 'd' * 32)
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
                # Simulate native heartbeat updates while slow Wine filesystem
                # flushes run; keep the separate stale-heartbeat refusals above.
                current = a.io.document(path)
                current['tick'] = a.ticks()
                a.io.save(path, current)
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
            a.io.save(path, dict(window, tick=a.ticks()))
            with a.lease(state / 'guard.lock'):
                agent.recover()
            assert not agent.pending.exists() and page.read_bytes() == after
            # Once the local receipt is durable, a lost cloud acknowledgement
            # must not pin the page or replay it over legitimate native edits.
            def applied_fault(stage):
                if stage == 'locally_applied':
                    raise InterruptedError('lost commit response')
            agent.fault = applied_fault
            a.io.save(path, dict(window, tick=a.ticks()))
            try:
                with a.lease(state / 'guard.lock'):
                    agent.start('SC', dict(id='2'*32, type='deposit_prepare', items=[]),
                                [(11, after, b'next authorized image')])
                raise AssertionError('fault did not interrupt the commit')
            except InterruptedError:
                pass
            pointer = a.io.document(agent.pending)
            assert pointer['pages'] == [11] and pointer['reload_safe'] is True
            record = a.io.document(state / 'transactions' / ('2'*32) / 'record.json')
            assert record['phase'] == 'applied'
            page.write_bytes(b'legitimate native item movement after delivery')
            handles.append(shared(state / 'live.lock'))
            agent.fault = lambda stage: None
            try:
                agent.run()  # Applied recovery precedes the unrelated native gate.
                raise AssertionError('Normal save access ignored the native lease')
            except BlockingIOError:
                pass
            close(handles.pop())
            assert not agent.pending.exists() and page.read_bytes() == b'legitimate native item movement after delivery'
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
