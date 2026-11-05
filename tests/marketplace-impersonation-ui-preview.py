"""Start an isolated real-HTTP owner/player fixture for impersonation UI checks."""
import importlib.util,json,socket,subprocess,sys,uuid
from pathlib import Path
repo=Path(__file__).resolve().parents[1];root=repo/'build-marketplace'/('impersonation-preview-'+uuid.uuid4().hex);root.mkdir(parents=True)
s=importlib.util.spec_from_file_location('web',Path(__file__).with_name('marketplace-web-http-test.py'));web=importlib.util.module_from_spec(s);s.loader.exec_module(web)
env,args,seed=web.fixture(Path(sys.argv[1]).resolve(),root)
setup=root/'owner.php';setup.write_text('<?php require '+json.dumps((web.SERVER/'bootstrap.php').as_posix())+';$m=market();$m->grantOwner('+json.dumps(seed['buyer']['id'])+');$a=$m->browserActor('+json.dumps(seed['seller']['id'])+');$m->browserLink(["code"=>$m->browserTicket($a)["code"]]);$m->run("UPDATE escrow_balances SET quantity=30724600025 WHERE owner=? AND kind=\'gold\'",['+json.dumps(seed['buyer']['id'])+']);$m->run("INSERT INTO escrow_balances VALUES (?, ?, \'gold\',\'\',30724600025)",['+json.dumps(seed['seller']['id'])+',"tcpip|RMD-MP|2.4|SC"]);',encoding='utf-8')
subprocess.check_call(args+[str(setup)],env=env)
router=root/'router.php';router.write_text('<?php if(parse_url($_SERVER["REQUEST_URI"],PHP_URL_PATH)==="/"){readfile('+json.dumps((web.SERVER/'public/market.html').as_posix())+');return true;}return false;',encoding='utf-8')
with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
with (root/'server.log').open('w') as log:p=subprocess.Popen(args+['-S',f'127.0.0.1:{port}','-t',str(web.SERVER/'public'),str(router)],env=env,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
meta={'root':str(root),'pid':p.pid,'url':f'http://127.0.0.1:{port}/#connect='+seed['code'],'target':seed['seller']['id'],'owner':seed['buyer']['id']};(repo/'build-marketplace/impersonation-preview.json').write_text(json.dumps(meta),encoding='utf-8');print(json.dumps(meta))
