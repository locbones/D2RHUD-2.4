"""Exercise the public Vault search UI against an isolated local PHP/SQLite service."""
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('web_fixture', REPO / 'tests/marketplace-web-http-test.py')
web_fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web_fixture)

with tempfile.TemporaryDirectory(prefix='market-vault-search-') as directory:
    root = Path(directory)
    env, php, seeded = web_fixture.fixture(REPO / 'build-marketplace/php-runtime/php.exe', root)
    seed = root / 'vault.php'
    seed.write_text('<?php require ' + json.dumps((web_fixture.SERVER / 'bootstrap.php').as_posix()) + ';' + r'''
$m=market();$fixture=json_decode(file_get_contents(__DIR__.'/fixture.json'),true);$seller=$fixture['seller'];
foreach([$seller,$fixture['buyer']] as $player)$m->browserLink(['code'=>$m->browserTicket($player)['code']]);
$fixture['seller_code']=$m->browserTicket($seller)['code'];file_put_contents(__DIR__.'/fixture.json',json_encode($fixture));
$row=$m->run('SELECT * FROM items WHERE owner=? LIMIT 1',[$seller['id']])->fetch();
$held=$m->run('SELECT * FROM escrow_items WHERE item=?',[$row['id']])->fetch();
$catalog=$m->browserCatalog();$named=array_filter($catalog['unique'],fn($d)=>stripos($d['name'],'stone of jordan')!==false);$index=(int)array_key_first($named);
for($n=1;$n<=150;++$n){
    $i=$row;$i['id']=str_pad(dechex($n),32,'0',STR_PAD_LEFT);$i['code']='rin';$i['quality']=$n>140?7:2;$i['item_index']=$n>140?$index:-1;$i['seed']=100000+$n;
    $m->run('INSERT INTO items ('.implode(',',array_keys($i)).') VALUES ('.implode(',',array_fill(0,count($i),'?')).')',array_values($i));
    $e=$held;$e['item']=$i['id'];$e['state']='available';$m->run('INSERT INTO escrow_items ('.implode(',',array_keys($e)).') VALUES ('.implode(',',array_fill(0,count($e),'?')).')',array_values($e));
}
''', encoding='utf-8')
    fixture = root / 'fixture.json'
    fixture.write_text(json.dumps(seeded), encoding='utf-8')
    subprocess.run(php + [str(seed)], env=env, check=True, capture_output=True)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    origin = f'http://127.0.0.1:{port}'
    with (root / 'server.log').open('w') as log:
        service = subprocess.Popen(php + ['-S', f'127.0.0.1:{port}', '-t', str(web_fixture.SERVER / 'public')], env=env, stdout=log, stderr=log)
        try:
            for _ in range(80):
                try:
                    urllib.request.urlopen(origin + '/web.php?action=markets', timeout=1).close()
                    break
                except OSError:
                    time.sleep(.1)
            with urllib.request.urlopen(origin + '/web.php?action=player-escrow&id=' + seeded['seller']['id'] + '&q=stone') as response:
                result = json.load(response)
            assert len(result['items']) >= 10 and result['query'] == 'stone'
            browser_env = dict(env, VAULT_SEARCH_ORIGIN=origin, VAULT_SEARCH_FIXTURE=str(fixture))
            subprocess.run(['node', str(REPO / os.environ.get('VAULT_SEARCH_BROWSER_TEST', 'tests/marketplace-vault-search-browser.cjs'))], cwd=REPO, env=browser_env, check=True, timeout=90)
        finally:
            service.terminate()
            service.wait(timeout=10)
