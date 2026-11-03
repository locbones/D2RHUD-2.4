"""Launch a hidden, disposable browser fixture for marketplace-live-ui-test.js."""
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys
import uuid


def main():
    repo = Path(__file__).resolve().parents[1]
    root = repo / 'build-marketplace' / ('seamless-preview-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    php = Path(sys.argv[1]).resolve()
    spec = importlib.util.spec_from_file_location('live_fixture', Path(__file__).with_name('marketplace-web-http-test.py'))
    web = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(web)
    env, args, seed = web.fixture(php, root)
    export = root / 'export.php'
    export.write_text('<?php require ' + json.dumps((web.SERVER / 'bootstrap.php').as_posix())
        + ';$m=market();$m->browserLink(["code"=>' + json.dumps(seed['code']) + ']);$a=$m->browserActor('
        + json.dumps(seed['buyer']['id']) + ');echo json_encode(["snapshot"=>$m->browserSnapshot($a,false),"markets"=>$m->modDirectory($a)]);', encoding='utf-8')
    fixture = json.loads(subprocess.check_output(args + [str(export)], env=env, text=True))
    (root / 'fixture.json').write_text(json.dumps(fixture), encoding='utf-8')
    router = root / 'router.php'
    router.write_text('''<?php
$path=parse_url($_SERVER['REQUEST_URI'],PHP_URL_PATH);
if($path==='/__live-harness.js'){header('Content-Type: application/javascript');readfile(%s);return true;}
if($path==='/__live-test'){$html=file_get_contents(%s);$json=str_replace('<','\\\\u003c',file_get_contents(__DIR__.'/fixture.json'));echo str_replace('<script src="market-players.js', '<script type="application/json" id="live-fixture">'.$json.'</script><script src="/__live-harness.js"></script><script src="market-players.js',$html);return true;}
return false;
''' % (json.dumps(Path(__file__).with_name('marketplace-live-ui-test.js').as_posix()),
       json.dumps((web.SERVER / 'public/market.html').as_posix())), encoding='utf-8')
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    with (root / 'server.log').open('w') as log:
        process = subprocess.Popen(args + ['-S', f'127.0.0.1:{port}', '-t', str(web.SERVER / 'public'), str(router)],
            env=env, stdout=log, stderr=log, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    metadata = dict(root=str(root), pid=process.pid, url=f'http://127.0.0.1:{port}/__live-test#browse')
    (repo / 'build-marketplace/seamless-preview.json').write_text(json.dumps(metadata), encoding='utf-8')
    print(json.dumps(metadata))


if __name__ == '__main__':
    main()
