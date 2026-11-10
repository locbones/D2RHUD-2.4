"""Real HTTP service and isolated save copies: one-page custody and currency parcels."""
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('original_agent_test', repo / 'tests/marketplace-agent-test.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
a = fixture.a


def main():
    php, helper, data, source = [Path(p).resolve() for p in sys.argv[1:]]
    original = source.read_bytes()
    inv = a.io.run_helper(helper, data, 'inventory', source)
    ring = next(i for i in inv['items'] if i['code'] == 'rin')
    shield = next(i for i in inv['items'] if i['quality'] == 7)
    cap = next(i for i in inv['items'] if i['code'] == 'cap')
    bag = next(i for i in inv['items'] if i['code'] == 'Z01' and i['balances'])
    price = [dict(kind='gold', code='gld', label='Gold', quality=2, index=-1, ethereal=-1, quantity=600)]
    with tempfile.TemporaryDirectory(prefix='market-one-page-') as temp:
        root = Path(temp)
        config = root / 'config.php'
        env = dict(os.environ, D2_MARKET_CONFIG=str(config), D2_MARKET_LOCAL_TEST='1')
        php_args = [str(php), '-d', 'extension_dir=' + str(php.parent / 'ext'), '-d', 'extension=pdo_sqlite']

        def community(name):
            config.write_text("<?php return ['dsn'=>" + json.dumps('sqlite:' + (root / 'market.sqlite').as_posix()) + ",'community'=>" + json.dumps(name) + '];', encoding='utf-8')

        def cli(*args):
            return json.loads(subprocess.check_output(php_args + [str(repo / 'tools/marketplace/server/cli.php'), *args], env=env, text=True))

        community('initial')
        cli('init')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        endpoint = f'http://127.0.0.1:{port}/index.php'
        with (root / 'http.log').open('w', encoding='utf-8') as log:
            server = subprocess.Popen(php_args + ['-S', f'127.0.0.1:{port}', '-t', str(repo / 'tools/marketplace/server/public')], env=env, stdout=log, stderr=log, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                for _ in range(100):
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=.1):
                            break
                    except OSError:
                        time.sleep(.05)

                def profile(name, items=(), gold=0, priced=None):
                    actor = cli('user', name)
                    pairing = cli('pair-agent', actor['id'], a.catalog(data))
                    folder = root / name / 'saves'
                    folder.mkdir(parents=True)
                    a.io.save(folder / 'custody-simulation.json', dict(simulation=True))
                    raw = bytearray(fixture.empty(original))
                    struct.pack_into('<I', raw, 12, gold)
                    for page in (11, 2):
                        (folder / f'Stash_SC_Page{page}.d2i').write_bytes(raw if page == 11 else fixture.empty(original))
                    settings = dict(schema=1, game_mode='2.4', mod='RMD-MP', hardcore=False, enabled=True, page=11, withdraw_page=2,
                                    auto_trade=False, drafts=[], agreements=[], simulation=dict(enabled=False, page=3, drafts=[]))
                    a.io.save(folder / 'Marketplace_2.4_SC.json', settings)
                    cfg = dict(simulation=True, state_directory=str(root / name / 'agent'), save_directory=str(folder), mod_data=str(data), helper=str(helper),
                               helper_sha256=a.io.digest(helper.read_bytes()), catalog=a.catalog(data), owner=actor['id'], gold_cap=2500000)
                    agent = a.Agent(cfg, lambda mode, packet: fixture.transport(endpoint, pairing['token'], mode, packet))
                    if items:
                        _, after, result = agent.plan('SC', settings, 11, credit_items=list(items))
                        agent.page('SC', 11).write_bytes(after)
                        if priced:
                            row = next(i for i in result['delivered'] if i['code'] == priced[0])
                            settings['drafts'] = [dict(tab=row['tab'], item_hex=row['item_hex'], price=priced[1], comment='Looking for the exact Codex chapter')]
                            a.io.save(folder / 'Marketplace_2.4_SC.json', settings)
                    return actor, agent

                def snapshot(actor):
                    return fixture.transport(endpoint, actor['token'], 'SC', None, 'sync')

                def command(actor, **packet):
                    return fixture.transport(endpoint, actor['token'], 'SC', dict(id=os.urandom(16).hex(), **packet), 'command')

                def inventory(agent):
                    return agent.inventory(agent.page('SC', 11))

                # Each interruption uses its own community and durable transfer journal.
                for point in ('intent', 'prepared_remote', 'authorized_remote', 'page_written', 'locally_applied', 'committed_remote'):
                    community(point)
                    seller, sa = profile('Seller-' + point, [ring, shield, cap], priced=('rin', price))
                    buyer, ba = profile('Buyer-' + point, gold=600)
                    page2 = sa.page('SC', 2).read_bytes()
                    once = [False]

                    def fail(stage):
                        if stage == point and not once[0]:
                            once[0] = True
                            raise InterruptedError(point)

                    sa.fault = fail
                    try:
                        sa.run()
                    except InterruptedError:
                        pass
                    assert once[0], point
                    sa.fault = lambda stage: None
                    sa.run()
                    assert sorted(i['code'] for i in inventory(sa)['items']) == sorted([shield['code'], 'cap'])
                    assert sa.settings('SC')['withdraw_page'] == 11
                    assert (sa.root / 'migrations/single-market-page-SC.json').exists()
                    assert sa.settings('SC')['drafts'] == []  # No stale draft can re-list a return.
                    ba.run()
                    listing = next(l for l in snapshot(buyer)['listings'] if l['seller_id'] == seller['id'])
                    assert listing['comment'] == 'Looking for the exact Codex chapter'
                    offer = command(buyer, type='propose', listing_id=listing['id'], listing_revision=int(listing['revision']), price=price)
                    command(seller, type='accept', offer_id=offer['offer_id'], expected_revision=1)
                    assert snapshot(buyer)['offers'][0]['status'] == 'completed'
                    original_access, original_transport = ba.save_access, ba.transport
                    operations = []
                    if point == 'intent':
                        # A genuine native-window test covers the lease authority;
                        # this isolated copy injects its acknowledged parking grant
                        # to verify the real HTTP delivery path does no unrelated work.
                        handoff = dict(hardcore=False, refresh_id='d'*32, session='delivery-priority-fixture')
                        @contextmanager
                        def access(mode=None, pages=None):
                            with original_access(mode, pages):
                                yield handoff
                        def transport(mode, packet):
                            operations.append(packet['type'])
                            return original_transport(mode, packet)
                        ba.save_access, ba.transport = access, transport
                        ba.timings.clear()
                        a.io.save(ba.root / 'live-refresh-request.json', dict(id='d'*32, key='priority-fixture', trigger='items:' + listing['item_id']))
                    once[0] = False
                    ba.fault = fail
                    try:
                        ba.run(preflight=point != 'intent')
                    except InterruptedError:
                        pass
                    assert once[0], 'delivery ' + point
                    ba.fault = lambda stage: None
                    ba.run(preflight=point != 'intent')
                    if point == 'intent':
                        assert 'web_sync' not in operations and 'deposit_prepare' not in operations
                        assert operations[0] == 'delivery_queue' and 'claim_prepare' in operations and 'write_authorize' in operations and 'commit' in operations
                        assert a.io.document(ba.root / 'live-refresh-result.json')['ready'] is True
                        assert a.io.document(ba.root / 'delivery-timing.json')['refresh_id'] == 'd'*32
                        ba.save_access, ba.transport = original_access, original_transport
                    ba.run()
                    received = inventory(ba)['items']
                    assert len(received) == 1 and received[0]['saved_id'] == ring['saved_id']
                    assert received[0]['saved_properties'] == ring['saved_properties'] and received[0]['socketed_items'] == ring['socketed_items']
                    command(seller, type='wallet_withdraw', balances=price)
                    sa.run()
                    assert inventory(sa)['gold'] == 600
                    stable = sa.page('SC', 11).read_bytes()
                    sa.run(); sa.run(preflight=True)
                    assert sa.page('SC', 11).read_bytes() == stable  # Returned gold is retained, never swept again.
                    assert sa.page('SC', 2).read_bytes() == page2
                    assert len(list((ba.root / 'transactions').glob('*/record.json'))) == 2

                # Explicit carried-to-market gold wakes a gold-only handoff and releases
                # an older withdrawal floor. Replaying the same request never debits again.
                for point in ('intent', 'prepared_remote', 'authorized_remote', 'page_written', 'locally_applied', 'committed_remote'):
                    community('manual-gold-' + point.replace(':', '-'))
                    owner, ga = profile('Manual-gold-' + point.replace(':', '-'), [cap], gold=600)
                    ga.migrate_market_page('SC')
                    a.io.save(ga.root / 'lineage.json', dict(delivered={}, consumed={}, receipts=[], retained_gold={'SC': dict(page=11, quantity=100)}))
                    handoff = dict(hardcore=False, deposit=11, delivery=11, loaded=1, refresh_id='b'*32, refresh_kind='gold_deposit',
                                   session='manual-gold-test', gold_deposit=dict(carried_before=1000, carried_after=500, stash_before=100, stash_after=600))
                    original_access = ga.save_access
                    @contextmanager
                    def gold_access(mode=None, pages=None):
                        with original_access(mode, pages): yield handoff
                    ga.save_access = gold_access
                    operations=[]; original_transport=ga.transport
                    def gold_transport(mode, packet):
                        operations.append(packet['type']);return original_transport(mode, packet)
                    ga.transport=gold_transport
                    fired=[False]
                    def interrupt(stage):
                        if not fired[0] and stage==point: fired[0]=True;raise InterruptedError(point)
                    ga.fault=interrupt
                    try: ga.run()
                    except InterruptedError: pass
                    assert fired[0], point
                    ga.fault=lambda stage: None
                    ga.run(); ga.run()
                    assert inventory(ga)['gold']==0
                    assert [i['code'] for i in inventory(ga)['items']]==['cap']
                    assert sum(b['quantity'] for b in snapshot(owner)['escrow']['balances'] if b['kind']=='gold')==600
                    assert a.io.document(ga.root/'lineage.json')['retained_gold']['SC']['quantity']==0
                    assert 'web_sync' not in operations and 'profile_sync' not in operations
                    assert a.io.document(ga.root/'live-refresh-result.json')['ready'] is True
                    assert len(list((ga.root/'transactions').glob('*/record.json')))==1
                    assert not ga.pending.exists()
                    # A late repeat of the completed handoff must not consume a new withdrawal.
                    saved=bytearray(ga.page('SC',11).read_bytes());struct.pack_into('<I',saved,12,100);ga.page('SC',11).write_bytes(saved)
                    retained=a.io.document(ga.root/'lineage.json');retained['retained_gold']['SC']=dict(page=11,quantity=100);a.io.save(ga.root/'lineage.json',retained)
                    ga.run();assert inventory(ga)['gold']==100
                    assert sum(b['quantity'] for b in snapshot(owner)['escrow']['balances'] if b['kind']=='gold')==600

                # A mismatched saved balance must not release a withdrawal floor.
                community('manual-gold-mismatch')
                owner, ga = profile('Manual-gold-mismatch', gold=600)
                ga.migrate_market_page('SC')
                a.io.save(ga.root/'lineage.json', dict(delivered={}, consumed={}, receipts=[], retained_gold={'SC':dict(page=11,quantity=100)}))
                bad_window=dict(hardcore=False,deposit=11,delivery=11,loaded=1,refresh_id='a'*32,refresh_kind='gold_deposit',session='bad-gold',
                                gold_deposit=dict(carried_before=1000,carried_after=400,stash_before=100,stash_after=700))
                original_access=ga.save_access
                @contextmanager
                def bad_access(mode=None,pages=None):
                    with original_access(mode,pages):yield bad_window
                ga.save_access=bad_access;ga.run()
                assert inventory(ga)['gold']==600 and a.io.document(ga.root/'lineage.json')['retained_gold']['SC']['quantity']==100
                assert not list((ga.root/'transactions').glob('*/record.json'))
                for invalid in (dict(carried_before=True,carried_after=0,stash_before=0,stash_after=1),dict(carried_before=100,carried_after=0,stash_before=0,stash_after=101)):
                    try:a.Agent.gold_deposit_proof(invalid)
                    except ValueError:pass
                    else:raise AssertionError('Invalid gold proof accepted')

                # Unpriced unique goods stay local until needed for a mutually agreed trade.
                community('agreed-barter')
                barter = [dict(code=shield['code'], label='Shield', quality=7, index=shield['index'], ethereal=-1, quantity=1)]
                seller, sa = profile('Barter-seller', [ring], priced=('rin', barter))
                buyer, ba = profile('Barter-buyer', [shield, cap])
                sa.run(); ba.run()
                assert len(inventory(ba)['items']) == 2 and snapshot(buyer)['escrow']['items'] == []
                listing = snapshot(buyer)['listings'][0]
                offer = command(buyer, type='propose', listing_id=listing['id'], listing_revision=int(listing['revision']), price=barter)
                ba.run(); assert len(inventory(ba)['items']) == 2
                command(seller, type='accept', offer_id=offer['offer_id'], expected_revision=1)
                ba.run(); sa.run(); ba.run()
                assert snapshot(buyer)['offers'][0]['status'] == 'completed'
                assert sorted(i['code'] for i in inventory(ba)['items']) == ['cap', 'rin']
                assert len(inventory(sa)['items']) == 1 and inventory(sa)['items'][0]['code'] == shield['code']

                # Resources auto-deposit, then return in a fresh container with a persisted UUID.
                community('currency-parcel')
                owner, agent = profile('Currency-owner', [bag, cap], gold=100)
                agent.run()
                wallet = snapshot(owner)['escrow']['balances']
                resource = next(r for r in wallet if r['kind'] == 'bag' and r['quantity'] > 0)
                amount = min(resource['quantity'], 123)
                balance = dict(kind='bag', stat=resource['stat'], code='Z01', label=resource['stat'], quality=2, index=-1, ethereal=-1, quantity=amount)
                gold100 = dict(price[0], quantity=100)
                command(owner, type='wallet_withdraw', balances=[balance, gold100])
                agent.run()
                rows = inventory(agent)
                bags = [i for i in rows['items'] if i['code'] == 'Z01']
                assert len(bags) == 2 and rows['gold'] == 100
                fresh = next(i for i in bags if i['item_uuid'])
                assert bytes.fromhex(fresh['item_hex'])[4] & 7 == 5, 'Currency bag must use modern native property dispatch'
                assert next(r['quantity'] for r in fresh['balances'] if r['stat'] == resource['stat']) == amount
                assert all(r['quantity'] == 0 for r in fresh['balances'] if r['stat'] != resource['stat'])
                assert a.io.document(agent.folder / 'MarketplaceReceipts_2.4_SC.json')['items']
                stable = agent.page('SC', 11).read_bytes()
                for _ in range(3):
                    agent.run()
                assert agent.page('SC', 11).read_bytes() == stable
                # Only newly added gold is deposited; previously delivered funds stay put.
                raw = bytearray(stable); struct.pack_into('<I', raw, 12, 150)
                agent.page('SC', 11).write_bytes(raw); agent.run()
                assert inventory(agent)['gold'] == 100
                assert next(r['quantity'] for r in snapshot(owner)['escrow']['balances'] if r['kind'] == 'gold') == 50
                assert source.read_bytes() == original
                # A settled, unchanged account needs one combined poll, with
                # no second inventory upload or duplicate delivery lookup.
                calls=[];original_transport=agent.transport
                def budget_transport(mode,packet):
                    if packet['type']=='web_poll':assert 'inventory' not in packet and len(json.dumps(packet))<2048
                    calls.append(packet['type']);return original_transport(mode,packet)
                agent.transport=budget_transport
                try:agent.run()
                finally:agent.transport=original_transport
                assert len(calls)==1 and calls[0] in {'web_sync','web_poll'},calls
                # A new watch-loop Agent reuses only its persisted fingerprint.
                restarted=a.Agent(agent.config,original_transport);stable=agent.page('SC',11).read_bytes();calls=[]
                def compact_transport(mode,packet):
                    calls.append(packet['type']);return original_transport(mode,packet)
                restarted.transport=compact_transport;restarted.run();assert calls==['web_poll'],calls
                # A stale server snapshot asks for a full upload without ever
                # treating the absent inventory as an empty inventory.
                calls=[]
                def stale_transport(mode,packet):
                    calls.append(packet['type'])
                    if packet['type']=='web_poll':return {'inventory_required':True}
                    return original_transport(mode,packet)
                restarted.transport=stale_transport;restarted.run();assert calls==['web_poll','web_sync'],calls
                assert restarted.page('SC',11).read_bytes()==stable
                calls=[]
                def legacy_transport(mode,packet):
                    calls.append(packet['type'])
                    if packet['type']=='web_poll':raise ValueError('Marketplace 422: Invalid text.')
                    return original_transport(mode,packet)
                restarted.transport=legacy_transport;restarted.run();assert calls==['web_poll','web_sync'],calls
                assert restarted.page('SC',11).read_bytes()==stable
                print('Single market page: legacy route migration, priced-only ordinary items, unlisted unique/superior retention, agreed barter funding, six deposit/delivery interruption boundaries, exactly-once receipts, unchanged item/socket properties, gold retention, matched manual-gold deposits with full-balance sweep and six interruption/replay checks, invalid-proof refusal, fresh UUID currency bags, no deposit/withdraw loops, old delivery page untouched and source immutability passed.')
            finally:
                server.terminate()
                server.wait(timeout=10)


if __name__ == '__main__':
    main()
