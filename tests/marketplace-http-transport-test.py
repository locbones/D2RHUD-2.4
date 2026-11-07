"""Production wire codec, real local PHP authentication/transactions, no live saves."""
import base64
import importlib.util
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid

repo = Path(__file__).resolve().parents[1]
def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module

wire = load('wire_test', repo/'tools/marketplace/marketplace-http.py')
agent = load('wire_agent', repo/'tools/marketplace/custody-agent.py')


class Opener:
    def __init__(self, result): self.result=result
    def open(self, request, timeout):
        assert timeout == 30
        if isinstance(self.result, Exception): raise self.result
        return io.BytesIO(self.result)


def main():
    value={'id':'same-request', 'inventory':[{'name':'Élite ✦', 'properties':list(range(300))}]}
    outer=json.loads(wire.encode(value))
    assert set(outer)=={'market_transport','payload'}
    assert json.loads(base64.b64decode(outer['payload']))==value
    for error, label in [(TimeoutError('private token'), 'timed out'),
                         (urllib.error.URLError(socket.gaierror(11001, 'private path')), 'resolve'),
                         (ConnectionResetError('private secret'), 'connection')]:
        try: wire.read_json(Opener(error), None, 'custody/web_sync (SC)');raise AssertionError('Network failure ignored')
        except ConnectionError as result:
            assert label in str(result) and 'web_sync' in str(result) and 'private' not in str(result)
    conflict=urllib.error.HTTPError('https://fixture',409,'conflict',{},io.BytesIO(b'{}'))
    try: wire.read_json(Opener(conflict),None,'request');raise AssertionError('Conflict ignored')
    except urllib.error.HTTPError as result: assert result is conflict
    assert wire.read_json(Opener(b'{"ready":true}'),None,'request')=={'ready':True}
    try: wire.read_json(Opener(b'x'*11),None,'request',limit=10);raise AssertionError('Response limit ignored')
    except ValueError: pass
    try: wire.encode({'data':'x'*100},limit=10);raise AssertionError('Request limit ignored')
    except ValueError: pass
    with tempfile.TemporaryDirectory(prefix='market-wire-') as temp:
        root=Path(temp)
        sentinel=root/'stash.d2i';sentinel.write_bytes(b'untouched game save')
        pending=root/'pending.json';pending.write_bytes(b'{"id":"preserve receipt"}')
        agent.io.save(root/'status.json',{'message':'Connecting to the game. Open the stash once.', 'result':{'SC':0}})
        agent.report_failure({'state_directory':str(root)},ConnectionError('Marketplace custody/web_sync timed out'))
        status=agent.io.document(root/'status.json')
        assert status['phase']=='retrying' and status['pending'] is True and 'message' not in status
        assert pending.read_bytes()==b'{"id":"preserve receipt"}' and sentinel.read_bytes()==b'untouched game save'

        php=Path(sys.argv[1]).resolve()
        fixture=load('wire_fixture',repo/'tests/marketplace-web-http-test.py')
        env,args,seeded=fixture.fixture(php,root/'service')
        seed=root/'agent.php'
        seed.write_text('<?php require '+json.dumps((fixture.SERVER/'bootstrap.php').as_posix())+'; $m=market();echo json_encode($m->pairAgent('+json.dumps(seeded['buyer']['id'])+',str_repeat("a",64)));',encoding='utf-8')
        pairing=json.loads(subprocess.check_output(args+[str(seed)],env=env,text=True))
        token=pairing['token'];catalog='a'*64
        with socket.socket() as sock: sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        with (root/'http.log').open('w') as log:
            process=subprocess.Popen(args+['-S',f'127.0.0.1:{port}','-t',str(fixture.SERVER/'public')],env=env,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                for _ in range(100):
                    try:
                        with socket.create_connection(('127.0.0.1',port),.1): break
                    except OSError: time.sleep(.05)
                def call(data,encoded=True,expected=200,bearer=token,action='custody',raw=None):
                    request=urllib.request.Request(f'http://127.0.0.1:{port}/index.php?action={action}&hardcore=0',
                        raw if raw is not None else wire.encode(data) if encoded else json.dumps(data).encode(),
                        {'Authorization':'Bearer '+bearer,'Content-Type':'application/json'})
                    try: response=urllib.request.urlopen(request,timeout=10)
                    except urllib.error.HTTPError as error: response=error
                    result=json.loads(response.read());assert response.code==expected,(response.code,result)
                    return result
                request={'id':uuid.uuid4().hex,'type':'delivery_queue','catalog':catalog,
                         'diagnostic_only':[{'id':n,'value':n} for n in range(1500)]}
                assert call(request)==call(request,False)
                bad=json.dumps({'market_transport':'json-base64-v1','payload':'!!!'}).encode()
                call({},raw=bad,expected=422)
                call({},raw=bad,bearer='0'*64,expected=401)  # Authentication precedes decoding.
                call({},raw=json.dumps({'market_transport':'unknown','payload':'e30='}).encode(),expected=422)
                call({},raw=wire.encode({'market_transport':'json-base64-v1','payload':'e30='}),expected=422)
                call({},raw=wire.encode(['x']),expected=422)
                call({},raw=wire.encode({'value':'x'*33000}),action='command',bearer=seeded['buyer']['token'],expected=413)
                call({'value':'x'*33000},encoded=False,action='command',bearer=seeded['buyer']['token'],expected=413)
                call({},raw=wire.encode({'nested':json.loads('['*34+'0'+']'*34)}),expected=422)
                # The exact transaction ID survives encode/decode. Duplicate
                # prepare/commit requests cannot create additional currency.
                gold=[{'kind':'gold','code':'gld','label':'Gold','quality':2,'index':-1,'ethereal':-1,'quantity':123}]
                transaction={'id':uuid.uuid4().hex,'catalog':catalog,'items':[],'balances':gold,
                    'pages':[{'page':11,'before':'b'*64,'after':'c'*64}]}
                prepared=call(dict(transaction,type='deposit_prepare'))
                assert call(dict(transaction,type='deposit_prepare'))==prepared
                call(dict(transaction,type='write_authorize'))
                committed=call(dict(transaction,type='commit'))
                assert call(dict(transaction,type='commit'))==committed
            finally:
                process.terminate();process.wait(timeout=5)
    print('PASS: flat wire roundtrip, legacy compatibility, real PHP authentication, payload/depth limits, transaction replay, network error context and preserved saves/receipts.')


if __name__=='__main__': main()
