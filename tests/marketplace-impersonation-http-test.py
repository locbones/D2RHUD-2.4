"""Owner impersonation over real PHP HTTP using disposable identities and SQLite."""
import http.cookiejar,importlib.util,json,os,socket,subprocess,sys,tempfile,time,urllib.error,urllib.request,uuid
from pathlib import Path
spec=importlib.util.spec_from_file_location('impersonation_web',Path(__file__).with_name('marketplace-web-http-test.py'));web=importlib.util.module_from_spec(spec);spec.loader.exec_module(web)
php=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='market-impersonation-http-') as temp:
    root=Path(temp);env,args,seed=web.fixture(php,root)
    def sql(code):
        f=root/'operator.php';f.write_text('<?php require '+json.dumps((web.SERVER/'bootstrap.php').as_posix())+';$m=market();'+code,encoding='utf-8')
        out=subprocess.check_output(args+[str(f)],env=env,text=True);return json.loads(out) if out else None
    ticket=sql('$m->grantOwner('+json.dumps(seed['buyer']['id'])+');echo json_encode($m->browserTicket($m->browserActor('+json.dumps(seed['seller']['id'])+')));')
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    endpoint=f'http://127.0.0.1:{port}/web.php'
    with (root/'http.log').open('w') as log:
        proc=subprocess.Popen(args+['-S',f'127.0.0.1:{port}','-t',str(web.SERVER/'public')],env=env,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            def client():return urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
            owner=client();target=client();csrf='';actor=''
            def call(action,data=None,expected=200,opener=None,token=None,acting=None,query=''):
                headers={} if data is None else {'Content-Type':'application/json','X-CSRF-Token':csrf if token is None else token,'X-Market-Actor':actor if acting is None else acting}
                if acting is False:headers.pop('X-Market-Actor',None)
                req=urllib.request.Request(endpoint+'?action='+action+query,None if data is None else json.dumps(data).encode(),headers=headers)
                try:
                    with (opener or owner).open(req,timeout=10) as r:status=r.status;body=json.load(r)
                except urllib.error.HTTPError as e:status=e.code;body=json.load(e)
                assert status==expected,(action,status,body);return body
            for _ in range(100):
                try:csrf=call('snapshot')['csrf'];break
                except (urllib.error.URLError,ConnectionError):time.sleep(.03)
            call('owner-impersonation',{'id':uuid.uuid4().hex,'type':'start','player':seed['seller']['id']},401)
            linked=call('link',{'code':seed['code']});csrf=linked['csrf'];actor=seed['buyer']['id']
            target_csrf=call('snapshot',opener=target)['csrf'];target_link=call('link',{'code':ticket['code']},opener=target,token=target_csrf,acting='');target_csrf=target_link['csrf']
            call('owner-impersonation',{'id':uuid.uuid4().hex,'type':'start','player':actor},403,opener=target,token=target_csrf,acting=seed['seller']['id'])
            packet={'id':uuid.uuid4().hex,'type':'start','player':seed['seller']['id']};old_csrf=csrf
            call('owner-impersonation',packet,403,token='bad-csrf')
            result=call('owner-impersonation',packet);csrf=result['csrf'];assert csrf!=old_csrf;session=result['impersonation']['id']
            assert result['impersonation']['owner']['id']==actor and result['impersonation']['player']['id']==seed['seller']['id'] and 'stamp' not in json.dumps(result)
            snapshot=call('snapshot');assert snapshot['self']['id']==seed['seller']['id'] and not snapshot['owner_tools'] and snapshot['escrow']['items']
            setup={'id':uuid.uuid4().hex,'type':'web_setup','expected_revision':0,'page':17,'withdraw_page':4,'enabled':False,'auto_trade':False}
            call('command',setup,409,acting=False) # An old browser build has no acting-player header.
            call('command',setup,409) # A stale owner tab must not operate on the target.
            call('command',setup,403,token=old_csrf,acting=seed['seller']['id'])
            for action,data in [('owner-command',{'id':uuid.uuid4().hex,'type':'clear_prepare'}),('link',{'code':'invalid'}),('unlink',{})]:call(action,data,403,acting=seed['seller']['id'])
            call('owner-status',expected=403);call('owner-impersonation-history',expected=403)
            actor=seed['seller']['id'];call('command',setup)
            assert call('snapshot')['profile']['desired']['page']==17
            item=snapshot['escrow']['items'][0]['item_id'];call('command',{'id':uuid.uuid4().hex,'type':'escrow_return','item_id':item})
            call('command',{'id':uuid.uuid4().hex,'type':'not_a_command'},422)
            private='Private support fixture';call('command',{'id':uuid.uuid4().hex,'type':'direct_message','to':seed['buyer']['id'],'message':private})
            # The target's own browser login and token were never modified.
            assert call('snapshot',opener=target)['self']['id']==seed['seller']['id']
            exited=call('owner-impersonation',{'type':'stop'},acting='');csrf=exited['csrf'];actor=seed['buyer']['id'];assert exited['impersonation'] is None
            restored=call('snapshot');assert restored['self']['id']==actor and restored['owner_tools'] and restored['profile']['settings']['page']==11
            assert sum(b['quantity'] for b in restored['escrow']['balances'] if b['kind']=='gold')==250000
            history=call('owner-impersonation-history',query='&id='+session)['sessions'][0]
            assert history['ended'] and len(history['actions'])==4 and private not in json.dumps(history) and 'stamp' not in json.dumps(history)
            assert {a['outcome'] for a in history['actions']}=={'completed','failed'} and all(a['owner']==actor and a['target']==seed['seller']['id'] for a in history['actions'])
            # Expiry restores the real owner automatically; old requests fail CSRF.
            result=call('owner-impersonation',{'id':uuid.uuid4().hex,'type':'start','player':seed['seller']['id']});csrf=result['csrf'];actor=seed['seller']['id'];sid=result['impersonation']['id']
            sql('$r=$m->run("SELECT result FROM market_owner_actions WHERE id=?",['+json.dumps(sid)+'])->fetchColumn();$r=json_decode($r,true);$r["expires"]=time()-1;$m->run("UPDATE market_owner_actions SET result=? WHERE id=?",[json_encode($r),'+json.dumps(sid)+']);')
            restored=call('snapshot');assert restored['self']['id']==seed['buyer']['id'] and restored['impersonation'] is None;csrf=restored['csrf'];actor=seed['buyer']['id']
            result=call('owner-impersonation',{'id':uuid.uuid4().hex,'type':'start','player':seed['seller']['id']});csrf=result['csrf'];actor=seed['seller']['id']
            sql('$m->run("DELETE FROM market_owners WHERE owner=?",['+json.dumps(seed['buyer']['id'])+']);')
            restored=call('snapshot');assert restored['self']['id']==seed['buyer']['id'] and restored['impersonation'] is None and not restored['owner_tools']
            csrf=restored['csrf'];actor=seed['buyer']['id'];call('owner-impersonation',packet,403)
            print('Impersonation HTTP: owner-only start, live target wallet/setup/return/message actions, author audit, no body/token leaks, original login preserved, exit/expiry/revocation and stale-tab/CSRF fences passed.')
        finally:proc.terminate();proc.wait(timeout=10)
