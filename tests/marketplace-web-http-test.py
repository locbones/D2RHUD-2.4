"""Browser session/API integration against a disposable local SQLite service."""
import http.cookiejar
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

REPO = Path(__file__).resolve().parents[1]
SERVER = REPO / 'tools/marketplace/server'


def fixture(php, root):
    root.mkdir(parents=True, exist_ok=True)
    config = root / 'config.php'
    config.write_text("<?php return ['dsn'=>" + json.dumps('sqlite:' + (root / 'market.sqlite').as_posix()) + "];", encoding='utf-8')
    env = dict(os.environ, D2_MARKET_CONFIG=str(config), D2_MARKET_LOCAL_TEST='1')
    args = [str(php), '-d', 'extension_dir=' + str(php.parent / 'ext'), '-d', 'extension=pdo_sqlite']
    catalog = json.loads((SERVER / 'public/assets/catalog.json').read_text(encoding='utf-8'))
    names = ['Harlequin Crest', "Mara's Kaleidoscope", 'The Stone of Jordan', 'Skin of the Vipermagi', 'War Traveler', 'Blacktongue']
    chosen = []
    for name in names:
        found = next(((int(i), r) for i, r in catalog['unique'].items() if r['name'].lower() == name.lower()), None)
        if found:
            chosen.append(dict(index=found[0], code=found[1]['code']))
    while len(chosen) < 6:
        i, row = list(catalog['unique'].items())[100 + len(chosen)]
        chosen.append(dict(index=int(i), code=row['code']))
    (root / 'catalog-picks.json').write_text(json.dumps(chosen), encoding='utf-8')
    seed = root / 'seed.php'
    seed.write_text("<?php require " + json.dumps((SERVER / 'bootstrap.php').as_posix()) + ";" + r'''
$m=market();$m->migrate();$seller=$m->user('The Wanderer');$buyer=$m->user('Development Player');$catalog=str_repeat('a',64);
$sa=$m->authAgent($m->pairAgent($seller['id'],$catalog)['token']);$ba=$m->authAgent($m->pairAgent($buyer['id'],$catalog)['token']);
$gold=fn($q)=>[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$q]];
$deposit=function($agent,$items,$balances=[])use($m,$catalog){$id=Market::id();$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];foreach(['deposit_prepare','write_authorize','commit'] as $type)$m->custody($agent,['id'=>$id,'type'=>$type,'catalog'=>$catalog,'items'=>$items,'balances'=>$balances,'pages'=>$pages],false);};
$items=[];$picks=json_decode(file_get_contents(__DIR__.'/catalog-picks.json'),true);
foreach($picks as $n=>$r)$items[]=['page'=>11,'tab'=>1,'saved_id'=>49000+$n,'quality'=>7,'index'=>$r['index'],'code'=>$r['code'],'ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'sockets'=>0,'item_hex'=>str_repeat(dechex(160+$n),24),'price'=>$gold(25000+5000*$n),'display'=>['level'=>85+$n,'picture'=>-1,'saved_properties'=>[['id'=>0,'value'=>15,'param'=>0,'list'=>'base'],['id'=>39,'value'=>35,'param'=>0,'list'=>'base'],['id'=>105,'value'=>20,'param'=>0,'list'=>'base']],'socketed_items'=>[]]];
$deposit($sa,$items);$deposit($ba,[], $gold(250000));
$local=['page'=>11,'tab'=>2,'saved_id'=>88001,'quality'=>6,'index'=>-1,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat('ee',24),'display'=>['level'=>87,'saved_properties'=>[['id'=>0,'value'=>20,'param'=>0,'list'=>'base'],['id'=>41,'value'=>31,'param'=>0,'list'=>'base']],'socketed_items'=>[]]];
$settings=['page'=>11,'withdraw_page'=>2,'enabled'=>true,'auto_trade'=>false];$m->custody($ba,['type'=>'web_sync','catalog'=>$catalog,'settings'=>$settings,'inventory'=>[$local]],false);
$snapshot=$m->snapshot($buyer,false);$listing=$snapshot['listings'][0];
$offer=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>$gold(20000),'message'=>'Would you trade for 20,000 gold?'],false);
$m->command($seller,['id'=>Market::id(),'type'=>'counter','offer_id'=>$offer['offer_id'],'expected_revision'=>1,'price'=>$gold(23000),'message'=>'I can do 23,000. This includes the exact item shown above.'],false);
for($i=0;$i<6;$i++){$r=$items[0];unset($r['display'],$r['price'],$r['quantity'],$r['quantity_bit'],$r['page'],$r['tab'],$r['identified']);$r['seed']=1200+$i;$r['sockets']=0;$l=$m->listing(['id'=>'operator','role'=>'admin'],['owner'=>$seller['id'],'item'=>$r,'price'=>$gold(18000+2000*$i)],false);$m->sale(['id'=>'operator','role'=>'admin'],['receipt'=>'fixture-sale-'.$i,'item_id'=>$l['item_id'],'buyer'=>$buyer['id'],'seller'=>$seller['id'],'price'=>$gold(18000+2000*$i)],false);}
echo json_encode(['code'=>$m->browserTicket($buyer)['code'],'buyer'=>$buyer,'seller'=>$seller,'offer'=>$offer,'listing'=>$listing]);
''', encoding='utf-8')
    seeded = json.loads(subprocess.check_output(args + [str(seed)], env=env, text=True))
    return env, args, seeded


def main():
    php = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='market-web-http-') as directory:
        root = Path(directory)
        env, args, seeded = fixture(php, root)
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            port = s.getsockname()[1]
        with (root / 'server.log').open('w') as log:
            proc = subprocess.Popen(args + ['-S', f'127.0.0.1:{port}', '-t', str(SERVER / 'public')], env=env, stdout=log, stderr=log,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                for _ in range(100):
                    try:
                        with socket.create_connection(('127.0.0.1', port), .1): break
                    except OSError: time.sleep(.05)
                endpoint = f'http://127.0.0.1:{port}/web.php'
                jar = http.cookiejar.CookieJar()
                client = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
                csrf = ''
                def call(action, data=None, expected=200, token=None, hc=0):
                    nonlocal csrf
                    req = urllib.request.Request(endpoint + f'?action={action}&hardcore={hc}', None if data is None else json.dumps(data).encode(),
                        {'Content-Type':'application/json', 'X-CSRF-Token': csrf if token is None else token})
                    try:
                        response = client.open(req, timeout=10)
                    except urllib.error.HTTPError as error:
                        assert error.code == expected, (error.code, error.read())
                        return json.load(error)
                    assert response.status == expected
                    value = json.load(response)
                    csrf = value.get('csrf', csrf)
                    return value
                guest = call('snapshot')
                call('owner-trades', expected=403)
                assert guest['self'] is None and guest['offers'] == [] and len(guest['listings']) == 6
                assert all('item_hex' not in row for row in guest['listings'])
                call('link', {'code':seeded['code']}, 403, token='bad-csrf')
                call('command', {'id':'0'*32,'type':'preferences','auto_trade':True}, 401)
                call('link', {'code':seeded['code']})
                session = call('snapshot')
                assert session['self']['id'] == seeded['buyer']['id']
                assert len(session['offers']) == 1 and len(session['local_items']) == 1
                unread_before = call('inbox')['unread']
                for key in ['a1'*16, 'a2'*16]:
                    call('command', {'id':key, 'type':'direct_message', 'to':seeded['buyer']['id'], 'message':'Read receipt HTTP fixture'})
                read_packet = {'id':'a3'*16, 'type':'message_read', 'messages':['a1'*16]}
                read_result = call('command', read_packet)
                assert read_result['unread'] == unread_before + 1 and call('command', read_packet) == read_result
                assert call('inbox')['unread'] == unread_before + 1
                call('command', {'id':'a4'*16, 'type':'message_read', 'messages':['a2'*16, 'af'*16]}, 422)
                assert call('inbox')['unread'] == unread_before + 1  # No partial batch acknowledgement.
                call('owner-trades', expected=403)
                grant = root / 'grant-owner.php'
                grant.write_text('<?php require ' + json.dumps((SERVER / 'bootstrap.php').as_posix()) +
                    ';market()->grantOwner(' + json.dumps(seeded['buyer']['id']) + ');', encoding='utf-8')
                subprocess.check_call(args + [str(grant)], env=env)
                assert call('snapshot')['owner_tools']
                audit_id = seeded['offer']['offer_id']
                assert call('owner-trade', expected=422)['error']
                # Use the real authenticated HTTP query path for detail/history.
                audit_url = endpoint + '?action=owner-trade&id=' + audit_id
                audit = json.load(client.open(audit_url, timeout=10))
                assert audit['status'] == 'pending' and audit['assets'] == []
                assert len(call('owner-trades')['rows']) == 7
                demo = call('owner-command', {'id':'9'*32,'type':'demo_generate','size':'small','expected_demo':''})
                assert demo['available'] and call('owner-status')['demo']['available']
                demo_file = root / 'operator-data/demo.sqlite'
                retained_demo = root / 'operator-data/demo.test-backup.sqlite'
                demo_file.replace(retained_demo)
                try:
                    demo_file.write_bytes(b'Invalid isolated demo fixture')
                    status = call('owner-status')
                    assert status['demo']['available'] is False and status['demo']['error']
                    assert 'counts' in status  # Optional demo failure cannot revoke live tools.
                finally:
                    demo_file.unlink(missing_ok=True)
                    retained_demo.replace(demo_file)
                # The linked player's unsynced HC profile must still accept setup.
                assert call('snapshot', hc=1)['profile'] is None
                setup = {'id':'5'*32, 'type':'web_setup', 'expected_revision':0,
                         'page':17, 'withdraw_page':4, 'enabled':True, 'auto_trade':False}
                receipt = call('command', setup, hc=1)
                assert receipt['queued'] and call('command', setup, hc=1) == receipt
                waiting = call('snapshot', hc=1)
                assert waiting['profile']['settings'] is None and waiting['profile']['seen'] == 0
                assert waiting['profile']['desired']['page'] == 17
                assert waiting['profile']['desired']['withdraw_page'] == 4
                assert waiting['pending'][0]['status'] == 'queued'
                assert call('snapshot')['profile'] == session['profile']
                public_id = session['self']['player_id']
                call('command', {'id':'4'*32,'type':'profile_name','name':'The Wanderer','expected_name':session['self']['name']})
                assert call('snapshot')['self']['player_id'] == public_id
                assert any(cookie.has_nonstandard_attr('HttpOnly') for cookie in jar)
                call('link', {'code':seeded['code']}, 401)
                offer = session['offers'][0]
                text = 'These terms look good.\nPlease keep this note with the trade. <b>plain text</b>'
                call('command', {'id':'3'*32,'type':'message','offer_id':offer['id'],'expected_revision':offer['revision'],'message':text})
                offer = call('snapshot')['offers'][0]
                assert offer['events'][-1]['message'] == text
                call('command', {'id':'1'*32,'type':'accept','offer_id':offer['id'],'expected_revision':offer['revision']})
                completed = call('snapshot')
                assert completed['offers'][0]['status'] == 'completed'
                assert len(completed['escrow']['items']) == 1
                assert sum(row['quantity'] for row in completed['escrow']['balances']) == 227000
                audit = json.load(client.open(audit_url, timeout=10))
                assert audit['status'] == 'completed' and len(audit['assets']) == 2
                assert {a['delivery']['state'] for a in audit['assets']} == {'pending', 'wallet'}
                assert 'item_hex' not in json.dumps(audit)
                assert call('snapshot', hc=1)['offers'] == []
                # A retired identity must not trap its browser in a failing
                # session: guest discovery and linking a current player work.
                retire = root / 'retire-session.php'
                retire.write_text('<?php require ' + json.dumps((SERVER / 'bootstrap.php').as_posix()) +
                    ';$m=market();$m->run("UPDATE actors SET role=\'retired\' WHERE id=?",[' + json.dumps(seeded['buyer']['id']) +
                    ']);$m->run("UPDATE market_owners SET owner=? WHERE community=?",[' + json.dumps(seeded['seller']['id']) +
                    ',"tcpip"]);echo json_encode($m->browserTicket($m->browserActor(' + json.dumps(seeded['seller']['id']) + ')));', encoding='utf-8')
                relink = json.loads(subprocess.check_output(args + [str(retire)], env=env, text=True))
                assert call('snapshot')['self'] is None
                call('link', {'code':relink['code']})
                assert call('snapshot')['self']['id'] == seeded['seller']['id']
                assert call('owner-status')['demo']['available']  # Cached old demo UID recovers.
                call('unlink', {})
                assert call('snapshot')['self'] is None
                call('command', {'id':'2'*32,'type':'preferences','auto_trade':True}, 401)
                print('Browser HTTP: shared names/stable IDs, retired-session recovery/relinking, guest discovery, CSRF, session rotation/cookies, single-use linking, private inventory, atomic settlement, SC/HC and unlink passed.')
            finally:
                proc.terminate()
                proc.wait(timeout=10)


if __name__ == '__main__':
    main()
