"""Real multi-process request limiting and deterministic overload recovery."""
import importlib.util, io, json, os, subprocess, sys, tempfile, threading, time, urllib.error, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest.mock import patch

repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('budget_http',repo/'tools/marketplace/marketplace-http.py')
wire=importlib.util.module_from_spec(spec);spec.loader.exec_module(wire)

if len(sys.argv)>1 and sys.argv[1]=='child':
    assert wire.read_json(urllib.request.build_opener(),urllib.request.Request(sys.argv[2]),'fixture',state_directory=sys.argv[3])=={'ok':True}
    sys.exit(0)

class Opener:
    def __init__(self,result):self.result=result;self.calls=0
    def open(self,request,timeout):
        self.calls+=1
        if isinstance(self.result,Exception):raise self.result
        return io.BytesIO(self.result)

with tempfile.TemporaryDirectory(prefix='market-network-budget-') as temp:
    root=Path(temp);pending=root/'pending.json';pending.write_text('{"id":"same-durable-receipt"}')
    sentinel=root/'stash.d2i';sentinel.write_bytes(b'untouched')
    clock=[1000.]
    with patch.object(wire.time,'time',side_effect=lambda:clock[0]),patch.object(wire.random,'uniform',return_value=1):
        error=urllib.error.HTTPError('https://private.invalid',503,'busy',{'Retry-After':'120'},io.BytesIO(b'{}'))
        failing=Opener(error)
        try:wire.read_json(failing,None,'commit',state_directory=root);raise AssertionError('503 ignored')
        except urllib.error.HTTPError:pass
        assert wire.network_state(root/'network-state.json')=={'failures':1,'retry_at':1120}
        other=Opener(b'{"ok":true}')
        for _ in range(100):
            try:wire.read_json(other,None,'sync',state_directory=root);raise AssertionError('Cooldown ignored')
            except ConnectionError:pass
        assert other.calls==0 and failing.calls==1
        clock[0]=1121
        assert wire.read_json(other,None,'sync',state_directory=root)=={'ok':True}
        assert wire.network_state(root/'network-state.json')=={}
        for expected in (5,10,20,40,80,120):
            try:wire.read_json(Opener(TimeoutError('secret')),None,'request',state_directory=root)
            except ConnectionError:pass
            record=wire.network_state(root/'network-state.json');assert record['retry_at']==clock[0]+expected
            clock[0]=record['retry_at']+1
        conflict=Opener(urllib.error.HTTPError('https://private.invalid',409,'conflict',{},io.BytesIO(b'{}')))
        try:wire.read_json(conflict,None,'commit',state_directory=root)
        except urllib.error.HTTPError as result:assert result.code==409
        assert not wire.in_cooldown(root)
        denied=urllib.error.HTTPError('https://private.invalid',403,'denied',{'Content-Type':'application/json'},io.BytesIO(b'{}'))
        try:wire.read_json(Opener(denied),None,'command',state_directory=root)
        except urllib.error.HTTPError as result:assert result.code==403
        assert not wire.in_cooldown(root),'Permission error became hosting overload'
        hosted=urllib.error.HTTPError('https://private.invalid',403,'denied',{'Content-Type':'text/html'},io.BytesIO(b'hosting block'))
        try:wire.read_json(Opener(hosted),None,'command',state_directory=root);raise AssertionError('HTML gateway refusal became a permanent command result')
        except ConnectionError:pass
        assert wire.in_cooldown(root)
        with patch.object(wire.random,'uniform',return_value=1.25):
            wire.cooldown(root/'network-state.json',{},error)
            assert wire.network_state(root/'network-state.json')['retry_at']==clock[0]+150,'Retry-After synchronized every client'
    assert pending.read_text()=='{"id":"same-durable-receipt"}' and sentinel.read_bytes()==b'untouched'
    (root/'network-state.json').unlink()
    active=peak=0;mutex=threading.Lock()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def do_GET(self):
            global active,peak
            with mutex:active+=1;peak=max(peak,active)
            time.sleep(.08);self.send_response(200);self.end_headers();self.wfile.write(b'{"ok":true}')
            with mutex:active-=1
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        args=[sys.executable,str(Path(__file__).resolve()),'child',f'http://127.0.0.1:{server.server_port}/',str(root)]
        children=[subprocess.Popen(args,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0)) for _ in range(4)]
        assert all(child.wait(timeout=10)==0 for child in children);assert peak==1
    finally:server.shutdown();server.server_close()
spec=importlib.util.spec_from_file_location('budget_agent',repo/'tools/marketplace/custody-agent.py')
agent=importlib.util.module_from_spec(spec);spec.loader.exec_module(agent)
with tempfile.TemporaryDirectory(prefix='market-change-wake-') as temp:
    root=Path(temp);folder=root/'saves';state=root/'state';folder.mkdir();state.mkdir()
    config={'save_directory':str(folder),'state_directory':str(state)}
    (folder/'Marketplace_2.4_SC.json').write_text('{"page":11,"withdraw_page":11}')
    page=folder/'Stash_SC_Page11.d2i';page.write_bytes(b'unchanged')
    window=state/'live-window.json';window.write_text('{"session":"one","loaded":11,"tick":1}')
    before=agent.watch_stamp(config)
    window.write_text('{"session":"one","loaded":11,"tick":9999,"at":9999}')
    assert agent.watch_stamp(config)==before,'Heartbeat became a server poll'
    page.write_bytes(b'gold deposited');changed=agent.watch_stamp(config);assert changed!=before
    window.write_text('{"session":"one","loaded":11,"refresh_id":"delivery"}')
    assert agent.watch_stamp(config)!=changed,'Actual handoff did not wake'
print('PASS network budget: shared-process HTTP serialization, 100 suppressed retries, Retry-After, exponential recovery, validation errors unchanged, real stash-change wakes, heartbeat suppression and save/journal preservation.')
