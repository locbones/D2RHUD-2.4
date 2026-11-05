"""First-run enrollment through the real HTTP boundary, on a disposable SQLite DB."""
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

repo = Path(__file__).resolve().parents[1]
server = repo / 'tools/marketplace/server'
php = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='market-enrollment-http-') as directory:
    root = Path(directory)
    config = root / 'config.php'
    config.write_text("<?php return ['dsn'=>" + json.dumps('sqlite:' + (root / 'market.sqlite').as_posix()) + '];', encoding='utf-8')
    env = dict(os.environ, D2_MARKET_CONFIG=str(config), D2_MARKET_LOCAL_TEST='1')
    args = [str(php), '-d', 'extension_dir=' + str(php.parent / 'ext'), '-d', 'extension=pdo_sqlite']
    subprocess.run(args + [str(server / 'cli.php'), 'init'], env=env, check=True, capture_output=True)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
    origin = 'http://127.0.0.1:' + str(port) + '/index.php'
    with (root / 'server.log').open('w') as log:
        process = subprocess.Popen(args + ['-S', '127.0.0.1:' + str(port), '-t', str(server / 'public')], env=env, stdout=log, stderr=log, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            for _ in range(100):
                try:
                    with socket.create_connection(('127.0.0.1', port), .1): break
                except OSError: time.sleep(.05)
            def call(action, data=None, expected=200, headers=None):
                request = urllib.request.Request(origin + '?action=' + action, None if data is None else json.dumps(data).encode(), headers or {'Content-Type':'application/json'})
                try:
                    response = urllib.request.urlopen(request, timeout=10)
                except urllib.error.HTTPError as error:
                    assert error.code == expected, (error.code, error.read())
                    return json.load(error)
                assert response.status == expected
                return json.load(response)
            payload = {'installation':'a' * 64, 'mod':'RMD-MP', 'game_mode':'2.4', 'catalog':'b' * 64}
            call('enroll', expected=405)
            call('enroll', payload, 415, {'Content-Type':'text/plain'})
            first = call('enroll', payload)
            assert call('enroll', payload) == first
            assert first['player']['role'] == 'player' and first['agent']['owner'] == first['player']['id']
            call('enroll', dict(payload, game_mode='3.x'), 409)
            call('sync', expected=401)
            call('enroll', dict(payload, catalog='c' * 64), 409)
            second = call('enroll', dict(payload, installation='d' * 64, role='admin', owner=first['player']['id']))
            assert second['player']['id'] != first['player']['id'] and second['player']['role'] == 'player'
            call('inbox', headers={'Authorization':'Bearer ' + first['player']['token']})
            print('Enrollment HTTP: automatic account setup, interrupted-response retries, independent players, working bearer credentials, method/content limits, version/catalog refusal and unchanged authenticated API passed.')
        finally:
            process.terminate(); process.wait(10)
