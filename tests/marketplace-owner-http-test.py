"""Owner controls and demo isolation through real CSRF-protected browser sessions."""
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

spec = importlib.util.spec_from_file_location('owner_fixture', Path(__file__).with_name('marketplace-web-http-test.py'))
web = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web)


def main():
    with tempfile.TemporaryDirectory(prefix='market-owner-http-') as directory:
        root = Path(directory)
        env, args, seeded = web.fixture(Path(sys.argv[1]).resolve(), root)
        script = root / 'owner.php'
        script.write_text('<?php require ' + json.dumps((web.SERVER / 'bootstrap.php').as_posix()) + ';$m=market();'
            + '$m->grantOwner(' + json.dumps(seeded['buyer']['id']) + ');$a=$m->browserActor(' + json.dumps(seeded['seller']['id'])
            + ");$m->grantModerator($a['id']);echo json_encode($m->browserTicket($a));", encoding='utf-8')
        other_code = json.loads(subprocess.check_output(args + [str(script)], env=env, text=True))['code']
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
                    def call(action, packet=None, expected=200, token=None, demo=False):
                        nonlocal csrf
                        request = urllib.request.Request(f'http://127.0.0.1:{port}/web.php?action={action}&environment={"demo" if demo else "live"}',
                            None if packet is None else json.dumps(packet).encode(),
                            {'Content-Type': 'application/json', 'X-CSRF-Token': csrf if token is None else token})
                        try:
                            response = client.open(request, timeout=30)
                        except urllib.error.HTTPError as error:
                            response = error
                        result = json.load(response)
                        assert response.code == expected, (response.code, result)
                        csrf = result.get('csrf', csrf)
                        return result
                    return call

                owner, other, guest = session(), session(), session()
                guest('owner-status', expected=403)
                guest('snapshot', expected=403, demo=True)
                owner('snapshot')
                owner('link', {'code': seeded['code']})
                other('snapshot')
                other('link', {'code': other_code})
                assert other('snapshot')['moderation']['moderator'] and not other('snapshot')['owner_tools']
                other('owner-status', expected=403)
                other('snapshot', expected=403, demo=True)
                assert owner('snapshot')['owner_tools']
                before = owner('snapshot')
                live_stats = guest('stats&window=all')
                packet = dict(id=uuid.uuid4().hex, type='demo_generate', size='busy', expected_demo='')
                owner('owner-command', packet, 403, token='bad')
                other('owner-command', packet, 403)
                demo = owner('owner-command', packet)
                assert demo['players'] == 65 and demo['listings'] == 320 and demo['sales'] == 240
                assert owner('owner-command', packet) == demo
                stats = owner('stats&window=all', demo=True)
                assert stats['summary']['trades'] == 240 and stats['summary']['active_listings'] == 320
                assert len(stats['recent']) == 10 and len(stats['popular']) == 8 and len(stats['sellers']) == 8
                assert guest('stats&window=all')['summary'] == live_stats['summary']
                assert 'item_hex' not in json.dumps(stats)
                scene = owner('snapshot', demo=True)
                assert len(scene['listings']) == 320 and scene['environment'] == 'demo'
                assert owner('snapshot') == before or len(owner('snapshot')['listings']) == 6
                featured = next(r for r in scene['listings'] if not r.get('bundle') and r['active_offer_count'] >= 2 and r['item']['index'] >= 0)
                history = owner('history&item_id=' + featured['item_id'] + '&averages=1', demo=True)
                assert len(history['rows']) >= 2 and history['averages']['sales'] > 0
                board = guest('listing-offers&listing_id=' + featured['id'], expected=404)
                assert board['error']  # Demo references cannot expose offers through the live market.
                listing = next(r for r in scene['listings'] if not r['own'] and r['price'][0].get('kind') == 'gold')
                offer = dict(id=uuid.uuid4().hex, type='propose', listing_id=listing['id'], listing_revision=1, price=listing['price'])
                receipt = owner('command', offer, demo=True)
                assert next(r for r in owner('snapshot', demo=True)['offers'] if r['id'] == receipt['offer_id'])['status'] == 'completed'
                owner('command', offer, demo=True)
                assert owner('stats&window=all', demo=True)['summary']['trades'] == 241
                assert len(owner('snapshot')['listings']) == 6
                clear = dict(id=uuid.uuid4().hex, type='demo_clear', expected_demo=demo['id'], confirm='wrong')
                owner('owner-command', clear, 409)
                clear.update(id=uuid.uuid4().hex, confirm='CLEAR DEMO')
                owner('owner-command', clear)
                owner('snapshot', expected=409, demo=True)
                assert not owner('owner-status')['demo']['available']
                start = owner('owner-command', dict(id=uuid.uuid4().hex, type='clear_prepare'))
                assert not start['ready_to_clear']
                owner('owner-command', dict(id=uuid.uuid4().hex, type='clear_finish', confirm='CLEAR MARKETPLACE', reset_id=start['control']['reset_id']), 409)
                owner('command', dict(id=uuid.uuid4().hex, type='preferences', auto_trade=True), 409)
                owner('owner-command', dict(id=uuid.uuid4().hex, type='clear_cancel', reset_id=start['control']['reset_id']))
                assert owner('owner-status')['control']['phase'] == 'live'
                print('Owner HTTP: guest/moderator denial, CSRF, isolated populated demo, bot settlement/retry dedupe, demo purge confirmation, guarded live reset and resume passed.')
            finally:
                process.terminate()
                process.wait(timeout=10)


if __name__ == '__main__':
    main()
