"""Report privacy and permissions through real browser sessions; disposable SQLite."""
import http.cookiejar
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid

spec = importlib.util.spec_from_file_location('web_fixture', Path(__file__).with_name('marketplace-web-http-test.py'))
web = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web)


def main():
    with tempfile.TemporaryDirectory(prefix='market-moderation-http-') as directory:
        root = Path(directory)
        env, args, seeded = web.fixture(Path(sys.argv[1]).resolve(), root)
        script = root / 'moderator.php'
        script.write_text('<?php require ' + json.dumps((web.SERVER / 'bootstrap.php').as_posix()) + ';'
                          + "$m=market();$a=$m->user('Moderator');$m->grantModerator($a['id']);echo json_encode($m->browserTicket($a));", encoding='utf-8')
        moderator = json.loads(subprocess.check_output(args + [str(script)], env=env, text=True))
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        with (root / 'server.log').open('w') as log:
            process = subprocess.Popen(args + ['-S', f'127.0.0.1:{port}', '-t', str(web.SERVER / 'public')], env=env,
                                       stdout=log, stderr=log, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                for _ in range(100):
                    try:
                        with socket.create_connection(('127.0.0.1', port), .1):
                            break
                    except OSError:
                        time.sleep(.05)

                def session():
                    client = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
                    csrf = ''

                    def call(action, packet=None, expected=200, token=None):
                        nonlocal csrf
                        req = urllib.request.Request(f'http://127.0.0.1:{port}/web.php?action={action}',
                            None if packet is None else json.dumps(packet).encode(),
                            {'Content-Type': 'application/json', 'X-CSRF-Token': csrf if token is None else token})
                        try:
                            response = client.open(req, timeout=10)
                        except urllib.error.HTTPError as error:
                            response = error
                        result = json.load(response)
                        assert response.code == expected, (response.code, result)
                        csrf = result.get('csrf', csrf)
                        return result
                    return call

                player, mod, guest = session(), session(), session()
                assert not guest('snapshot')['moderation']['moderator']
                guest('reports', expected=401)
                player('snapshot')
                player('link', {'code': seeded['code']})
                player('reports', expected=403)
                row = player('snapshot')['listings'][0]
                report = dict(id=uuid.uuid4().hex, type='report_player', subject=seeded['seller']['id'],
                    listing_id=row['id'], listing_revision=int(row['revision']), reason='modified_item',
                    details='HTTP test: these properties look unusual. <script>escaped evidence</script>')
                player('command', report, 403, token='invalid')
                receipt = player('command', report)
                assert player('command', report) == receipt
                player('report&id=' + receipt['report_id'], expected=403)
                guest('report&id=' + receipt['report_id'], expected=401)
                assert 'reports' not in player('snapshot')
                mod('snapshot')
                mod('link', {'code': moderator['code']})
                assert mod('snapshot')['moderation'] == dict(moderator=True, open_count=1)
                queue = mod('reports')['reports']
                assert len(queue) == 1 and 'evidence' not in queue[0]
                detail = mod('report&id=' + receipt['report_id'])
                assert detail['details'] == report['details']
                assert 'item_hex' not in json.dumps(detail) and 'token' not in json.dumps(detail)
                review = dict(id=uuid.uuid4().hex, type='review_report', report_id=detail['id'],
                              expected_revision=1, status='resolved', note='Private moderator decision.')
                player('command', review, 403)
                mod('command', review, 403, token='invalid')
                mod('command', review)
                assert not mod('reports')['reports']
                assert mod('report&id=' + receipt['report_id'])['events'][-1]['note'] == review['note']
                assert review['note'] not in json.dumps(guest('snapshot'))
                mod('reports&before=bad', expected=422)
                mod('reports&status=invalid', expected=422)
                print('Moderation HTTP: guests/players denied private queue and evidence, CSRF enforced, moderator-only reviews, idempotent reports, exact snapshots and private decisions passed.')
            finally:
                process.terminate()
                process.wait(timeout=10)


if __name__ == '__main__':
    main()
