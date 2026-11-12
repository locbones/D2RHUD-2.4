"""Real browser/bearer routes: detected mods and version/catalog isolation."""
import http.cookiejar,importlib.util,json,os,socket,subprocess,sys,tempfile,time,urllib.error,urllib.parse,urllib.request
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('web_fixture',repo/'tests/marketplace-web-http-test.py');web=importlib.util.module_from_spec(spec);spec.loader.exec_module(web)
php=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='market-mod-http-') as temp:
 root=Path(temp);env,args,seed=web.fixture(php,root);(root/'base.json').write_text(json.dumps(seed),encoding='utf-8')
 setup=root/'mods.php';setup.write_text('<?php require '+json.dumps((web.SERVER/'bootstrap.php').as_posix())+";$m=market();$d=json_decode(file_get_contents(__DIR__.'/base.json'),true);$o=new Market($m->db,'tcpip','Detected Mod');echo json_encode($o->pairAgent($d['seller']['id'],str_repeat('b',64)));",encoding='utf-8')
 agent=json.loads(subprocess.check_output(args+[str(setup)],env=env,text=True))
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 with (root/'server.log').open('w') as log:
  process=subprocess.Popen(args+['-S',f'127.0.0.1:{port}','-t',str(web.SERVER/'public')],env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
  try:
   for _ in range(100):
    try:
     with socket.create_connection(('127.0.0.1',port),.1):break
    except OSError:time.sleep(.05)
   client=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()));csrf=''
   def call(action,data=None,status=200,mod='RMD-MP',version='2.4',bearer=None):
    global csrf
    query=urllib.parse.urlencode(dict(action=action,mod=mod,version=version));endpoint='index.php' if bearer else 'web.php'
    headers={'Content-Type':'application/json','X-CSRF-Token':csrf}
    if bearer:headers['Authorization']='Bearer '+bearer
    request=urllib.request.Request(f'http://127.0.0.1:{port}/{endpoint}?{query}',None if data is None else json.dumps(data).encode(),headers)
    try:response=client.open(request,timeout=10)
    except urllib.error.HTTPError as error:response=error
    assert response.code==status,(response.code,response.read())
    if status==503:assert response.headers.get('Retry-After')=='15'
    value=json.load(response);csrf=value.get('csrf',csrf);return value
   assert [m['mod'] for m in call('markets')['mods']]==['RMD-MP']
   initial=call('bootstrap',mod='unknown')
   assert initial['mod']=='RMD-MP' and initial['directory']['self'] is None and len(initial['snapshot']['listings'])==6
   assert call('bootstrap',version='3.x')['snapshot'] is None
   holder=subprocess.Popen(args+['-r','require $argv[1]; $a=marketReadSlot(); $b=marketReadSlot(); echo "ready\\n"; fgets(STDIN);',str(web.SERVER/'ReadBudget.php')],
       env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
   try:
    assert holder.stdout.readline().strip()=='ready'
    call('bootstrap',status=503)
    call('sync',bearer=seed['seller']['token'],status=503)
    assert call('markets')['mods'] # Cheap traffic keeps its path to PHP.
   finally:
    holder.stdin.close();holder.wait(timeout=10)
   assert call('bootstrap')['snapshot']['mod']=='RMD-MP'
   call('snapshot',mod='unknown',status=404);call('snapshot',version='3.x',status=409);call('command',{'id':'0'*32,'type':'preferences','auto_trade':True},version='3.x',status=409)
   definitions=json.loads((web.SERVER/'item-identity.json').read_text(encoding='utf-8'))
   context=dict(mod='Detected Mod',game_mode='2.4',catalog='b'*64,identity_catalog=definitions)
   call('browser-ticket',context,bearer=seed['buyer']['token'],status=403)
   ticket=call('browser-ticket',context,bearer=seed['seller']['token']);assert ticket['mod']=='Detected Mod'
   linked=call('link',dict(code=ticket['code']));assert linked['mod']=='Detected Mod' and linked['self']['id']==seed['seller']['id']
   initial=call('bootstrap',mod='Detected Mod')
   assert initial['directory']['self']['id']==seed['seller']['id'] and initial['snapshot']['self']['id']==seed['seller']['id'] and initial['snapshot']['linked_mod']
   assert 'Detected Mod' in [m['mod'] for m in call('markets')['mods']]
   snap=call('snapshot',mod='Detected Mod');assert snap['mod']=='Detected Mod' and not snap['listings'] and snap['linked_mod']
   assert len(call('snapshot')['listings'])==6
   catalog=call('catalog',mod='Detected Mod');assert not catalog['stats'] and all(i['image'] is None for i in catalog['bases'].values())
   call('custody',dict(type='profile_state',catalog='b'*64),bearer=agent['token'],status=403)
   assert call('custody',dict(type='profile_state',catalog='b'*64),mod='Detected Mod',bearer=agent['token'])['linked']
   call('custody',dict(type='profile_state',catalog='b'*64),mod='Detected Mod',version='3.x',bearer=agent['token'],status=409)
   assert call('sync',mod='Detected Mod',bearer=seed['seller']['token'])['mod']=='Detected Mod'
   print('Scoped HTTP: dynamic detected mod from paired catalog, one-use mod link, public directory, scoped catalog/snapshots, bearer-agent namespace enforcement, unpaired-context refusal and server-side 3.x refusal passed.')
  finally:process.terminate();process.wait(timeout=10)
