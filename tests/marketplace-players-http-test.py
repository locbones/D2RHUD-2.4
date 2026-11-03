"""Player profiles and private native mail over real PHP HTTP, isolated SQLite/saves."""
import hashlib,http.cookiejar,importlib.util,json,os,socket,subprocess,sys,tempfile,time,urllib.error,urllib.parse,urllib.request
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
def module(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
web=module('players_web_fixture',repo/'tests/marketplace-web-http-test.py')
sync=module('players_sync',repo/'tools/marketplace/sync-marketplace.py')
agent=module('players_agent',repo/'tools/marketplace/custody-agent.py')
php,helper,data,save=map(lambda p:Path(p).resolve(),sys.argv[1:5])
original=save.read_bytes()
with tempfile.TemporaryDirectory(prefix='market-players-http-') as temp:
    root=Path(temp);env,args,seed=web.fixture(php,root)
    (root/'base.json').write_text(json.dumps(seed),encoding='utf-8')
    setup=root/'profiles.php';setup.write_text('<?php require '+json.dumps((web.SERVER/'bootstrap.php').as_posix())+r''';
$m=market();$d=json_decode(file_get_contents(__DIR__.'/base.json'),true);$a=$d['seller'];$b=$d['buyer'];$m->browserLink(['code'=>$m->browserTicket($a)['code']]);
$agent=$m->pairAgent($a['id'],str_repeat('a',64));
echo json_encode(['agent'=>$agent,'unlinked'=>$m->user('Unlinked')]);
''',encoding='utf-8')
    extra=json.loads(subprocess.check_output(args+[str(setup)],env=env,text=True))
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    log=(root/'server.log').open('w');process=subprocess.Popen(args+['-S',f'127.0.0.1:{port}','-t',str(web.SERVER/'public')],env=env,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    try:
        for _ in range(100):
            try:
                with socket.create_connection(('127.0.0.1',port),.1):break
            except OSError:time.sleep(.05)
        base=f'http://127.0.0.1:{port}/';jar=http.cookiejar.CookieJar();browser=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar));csrf=''
        def call(action,body=None,actor=None,expected=200,query=None):
            global csrf
            url=base+('index.php' if actor else 'web.php')+'?'+urllib.parse.urlencode(dict({'action':action,'hardcore':0},**(query or {})))
            headers={'Content-Type':'application/json','X-CSRF-Token':csrf}
            if actor:headers['Authorization']='Bearer '+actor['token']
            request=urllib.request.Request(url,None if body is None else json.dumps(body).encode(),headers)
            try:r=(urllib.request.urlopen(request,timeout=15) if actor else browser.open(request,timeout=15))
            except urllib.error.HTTPError as e:
                assert e.code==expected,(action,e.code,e.read());return json.load(e)
            assert r.status==expected;value=json.load(r);csrf=value.get('csrf',csrf);return value
        assert len(call('players')['players'])==1
        call('inbox',expected=401);call('messages',expected=401)
        call('snapshot');call('link',{'code':seed['code']})
        assert len(call('players')['players'])==2
        # The actual publisher reads an immutable D2S copy and sends display data only.
        saves=root/'saves';saves.mkdir();copied=saves/save.name;copied.write_bytes(original)
        a=agent.Agent.__new__(agent.Agent);a.root=root/'state';a.root.mkdir();a.folder=saves.resolve();a.helper=helper;a.data=data
        def custody(mode,body):return call('custody',dict(body,catalog='a'*64),extra['agent'],query={'hardcore':int(mode=='HC')})
        a.request=custody;a.publish_characters()
        state=json.loads((a.root/'profile-sync.json').read_text());key=state[save.name]['key'];profile=call('player',query={'id':seed['seller']['id'],'character':key})
        character=profile['character'];assert character['equipment'] and character['inventory'] and character['mercenary']['equipment']
        assert not any(x in json.dumps(profile) for x in ['item_hex','source_directory','stash','cube_contents'])
        assert copied.read_bytes()==original and save.read_bytes()==original
        a.publish_characters();assert json.loads((a.root/'profile-sync.json').read_text())==state
        copied.write_bytes(b'corrupt');a.publish_characters();assert call('player',query={'id':seed['seller']['id'],'character':key})['character']==character
        copied.unlink();a.publish_characters();assert call('player',query={'id':seed['seller']['id']})['characters']==[]
        # Native outbox is delivered once; incoming direct/trade messages are private.
        call('command',{'id':'a'*32,'type':'direct_message','to':seed['buyer']['id'],'message':'A private UTF-8 greeting é 中 <script>inert</script>'},seed['seller'])
        settings={'endpoint':base+'index.php','player_id':seed['buyer']['id'],'token_env':'MARKET_PLAYERS_TEST_TOKEN','mod':'RMD-MP'};os.environ['MARKET_PLAYERS_TEST_TOKEN']=seed['buyer']['token']
        outbox=saves/'MarketplaceChatOutbox_2.4_SC.json'
        packet={'schema':1,'game_mode':'2.4','mod':'RMD-MP','hardcore':False,'owner':seed['buyer']['id'],'commands':[{'id':'b'*32,'type':'direct_message','to':seed['seller']['id'],'message':'Reply from native outbox','hardcore':False}]};outbox.write_text(json.dumps(packet),encoding='utf-8')
        sync.sync(settings,saves,allow_local=True);sync.sync(settings,saves,allow_local=True)
        incoming=json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text());assert len([m for m in incoming['messages'] if m['kind']=='direct'])==1 and any(m['body'].startswith('A private UTF-8') for m in incoming['messages'])
        assert any(m['kind']=='trade' and m['thread']==seed['offer']['offer_id'] and m['body'].startswith('Counter-offer:') for m in incoming['messages'])
        assert incoming['results']['b'*32]['peer']==seed['seller']['id']
        assert len([m for m in call('inbox',actor=seed['seller'])['messages'] if m['kind']=='direct'])==1
        assert call('inbox',actor=extra['unlinked'])['messages']==[]
        sequence=incoming['messages'][-1]['seq'];(saves/'MarketplaceChatSeen_2.4.json').write_text(json.dumps({'owner':seed['buyer']['id'],'through':sequence}))
        sync.sync(settings,saves,allow_local=True);assert json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages']==[]
        call('messages',query={'peer':seed['seller']['id']});call('command',{'id':'c'*32,'type':'message_read','through':sequence})
        assert call('inbox')['unread']==0
        # A browser self-message takes the real companion inbox path exactly
        # once, rather than being redirected or discarded as an outgoing echo.
        self_packet={'id':'e'*32,'type':'direct_message','to':seed['buyer']['id'],'message':'Self-test: website to my in-game chat'}
        self_result=call('command',self_packet);assert call('command',self_packet)==self_result
        sync.sync(settings,saves,allow_local=True);sync.sync(settings,saves,allow_local=True)
        self_cache=json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages']
        self_rows=[m for m in self_cache if m['id']=='e'*32]
        assert len(self_rows)==1 and self_rows[0]['sender']==self_rows[0]['recipient']==seed['buyer']['id']
        assert len(call('messages',query={'peer':seed['buyer']['id']})['messages'])==1
        assert not any(m['id']=='e'*32 for m in call('inbox',actor=seed['seller'])['messages'])
        sequence=self_rows[0]['seq'];(saves/'MarketplaceChatSeen_2.4.json').write_text(json.dumps({'owner':seed['buyer']['id'],'through':sequence}))
        sync.sync(settings,saves,allow_local=True);assert json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages']==[]
        call('command',{'id':'f'*32,'type':'message_read','through':sequence});assert call('inbox')['unread']==0
        # A real counter and acceptance settle against isolated escrow. Both
        # parties receive completion, then the native mailbox exposes it once.
        counter={'id':'1'*32,'type':'counter','offer_id':seed['offer']['offer_id'],'expected_revision':2,'price':[{'kind':'gold','code':'gld','quality':2,'index':-1,'ethereal':-1,'quantity':21000,'label':'Gold'}],'message':'My counter from the website'}
        result=call('command',counter);assert call('command',counter)==result
        seller_inbox=call('inbox',actor=seed['seller'])['messages']
        assert any(m['thread']==seed['offer']['offer_id'] and m['body'].startswith('Counter-offer:') and '21000 × Gold' in m['body'] for m in seller_inbox)
        accept={'id':'2'*32,'type':'accept','offer_id':seed['offer']['offer_id'],'expected_revision':3}
        result=call('command',accept,seed['seller']);assert result['status']=='completed' and call('command',accept,seed['seller'])==result
        sync.sync(settings,saves,allow_local=True);sync.sync(settings,saves,allow_local=True)
        updates=json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages']
        assert len([m for m in updates if m['body'].startswith('Offer accepted:')])==1
        assert len([m for m in updates if m['body'].startswith('Trade completed:')])==1
        assert not any(m['body'].startswith('Offer closed:') and m['thread']==seed['offer']['offer_id'] for m in updates)
        assert any(m['body'].startswith('Trade completed:') for m in call('inbox',actor=seed['seller'])['messages'])
        assert call('inbox',actor=extra['unlinked'])['messages']==[]
        sequence=updates[-1]['seq'];(saves/'MarketplaceChatSeen_2.4.json').write_text(json.dumps({'owner':seed['buyer']['id'],'through':sequence}))
        sync.sync(settings,saves,allow_local=True);assert json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages']==[]
        call('command',{'id':'3'*32,'type':'message_read','through':sequence})
        # An offline game's bounded buffer pauses without skipping server rows.
        setup.write_text('<?php require '+json.dumps((web.SERVER/'bootstrap.php').as_posix())+r''';
$m=market();$d=json_decode(file_get_contents(__DIR__.'/base.json'),true);for($n=0;$n<260;$n++)$m->run("INSERT INTO market_messages(id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)",[md5('backlog'.$n),$d['seller']['id'],$d['buyer']['id'],'','direct',str_repeat('f',64),'Offline message '.$n,time()-600]);echo json_encode($m->run('SELECT seq FROM market_messages WHERE recipient=? AND seq>? ORDER BY seq',[$d['buyer']['id'],%d])->fetchAll(PDO::FETCH_COLUMN));
'''%sequence,encoding='utf-8')
        expected=json.loads(subprocess.check_output(args+[str(setup)],env=env,text=True));assert len(expected)==260
        for _ in range(3):sync.sync(settings,saves,allow_local=True)
        paused=json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages'];assert len(paused)==200 and [m['seq'] for m in paused]==expected[:200]
        (saves/'MarketplaceChatSeen_2.4.json').write_text(json.dumps({'owner':seed['buyer']['id'],'through':expected[99]}))
        sync.sync(settings,saves,allow_local=True);resumed=json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages'];assert [m['seq'] for m in resumed]==expected[100:]
        assert sorted(set(m['seq'] for m in paused+resumed))==expected
        # Reply from an HC game to a SC trade: only the explicit thread scope wins.
        hc_packet=dict(packet,hardcore=True,commands=[{'id':'d'*32,'type':'trade_reply','offer_id':seed['offer']['offer_id'],'message':'Cross-mode native reply','hardcore':False}])
        (saves/'MarketplaceChatOutbox_2.4_HC.json').write_text(json.dumps(hc_packet))
        sync.sync(settings,saves,hardcore=True,allow_local=True)
        hc_cache=json.loads((saves/'MarketplaceMessages_2.4_HC.json').read_text());assert hc_cache['results']['d'*32]['offer_id']==seed['offer']['offer_id']
        # Suppressed rows must advance the native cursor, and preferences also
        # apply to the existing offline buffer without deleting the web archive.
        call('command',{'id':'4'*32,'type':'notification_settings','game_direct':False,'game_trade':True,'game_search':True})
        sync.sync(settings,saves,allow_local=True)
        assert json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages']==[]
        (saves/'MarketplaceChatSeen_2.4.json').write_text(json.dumps({'owner':seed['buyer']['id'],'through':expected[-1]}))
        setup.write_text('<?php require '+json.dumps((web.SERVER/'bootstrap.php').as_posix())+r''';
$m=market();$d=json_decode(file_get_contents(__DIR__.'/base.json'),true);for($n=0;$n<101;$n++)$m->run("INSERT INTO market_messages(id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)",[md5('muted'.$n),$d['seller']['id'],$d['buyer']['id'],'','direct','','Muted fixture '.$n,time()]);$m->run("INSERT INTO market_messages(id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)",[md5('after-muted'),$d['seller']['id'],$d['buyer']['id'],$m->scope(false),'trade','','Trade after muted messages',time()]);
''',encoding='utf-8')
        subprocess.check_call(args+[str(setup)],env=env)
        sync.sync(settings,saves,allow_local=True);sync.sync(settings,saves,allow_local=True)
        filtered=json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages']
        assert len(filtered)==1 and filtered[0]['body']=='Trade after muted messages'
        call('command',{'id':'5'*32,'type':'notification_settings','game_direct':False,'game_trade':False,'game_search':True})
        sync.sync(settings,saves,allow_local=True)
        assert json.loads((saves/'MarketplaceMessages_2.4_SC.json').read_text())['messages']==[]
        # Guest still sees previews, but no inbox or messages, after unlinking.
        call('unlink',{});call('messages',expected=401);call('inbox',expected=401)
        assert save.read_bytes()==original
        print('Players HTTP: code-linked public profiles, actual read-only character publisher/cache/removal, invalid-save retention, guest/private authorization, UTF-8 native mail and trade-status delivery, self-message/counter/completion retry dedupe, lossless bounded offline buffering, cross-mode trade replies, read cursors, full-buffer preference changes, filtered cursor progress and unchanged real saves passed.')
    finally:process.terminate();process.wait(timeout=10);log.close()
