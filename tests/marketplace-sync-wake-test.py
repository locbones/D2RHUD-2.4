"""Native commands/closure trigger sync without waiting for the polling interval."""
import importlib.util
from pathlib import Path
import json
import tempfile
import threading
import time

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('market_sync', repo / 'tools/marketplace/sync-marketplace.py')
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
with tempfile.TemporaryDirectory(prefix='market-sync-wake-') as temporary:
    folder = Path(temporary)
    config = {'custody_status': str(folder / 'status.json')}
    config['player_id'] = 'fixture'
    before = s.watch_stamp(config, folder)
    def publish():
        time.sleep(.1)
        (folder / 'MarketplaceActivityLocal_2.4_SC.json').write_text('{}', encoding='utf-8')
    producer = threading.Thread(target=publish)
    producer.start()
    started = time.monotonic()
    s.wait_for_producer(config, folder, False, before, 15)
    producer.join()
    assert time.monotonic() - started < 1
    presence = folder / 'MarketplaceChatPresence_2.4.json'
    idle = s.watch_stamp(config, folder)
    assert not idle[-1]
    presence.write_text(json.dumps({'owner':'fixture','at':int(time.time())}),encoding='utf-8')
    active = s.watch_stamp(config, folder)
    assert active[-1] and active != idle
    presence.write_text(json.dumps({'owner':'fixture','at':int(time.time())-1}),encoding='utf-8')
    assert s.watch_stamp(config, folder) == active  # Refreshing presence is not an extra network wake.
    presence.write_text(json.dumps({'owner':'fixture','at':int(time.time())-16}),encoding='utf-8')
    assert s.watch_stamp(config, folder) == idle
    (folder / 'live-window.json').write_text(json.dumps({'session': 'one', 'tick': 1}), encoding='utf-8')
    epoch = s.watch_stamp(config, folder)
    (folder / 'live-window.json').write_text(json.dumps({'session': 'one', 'tick': 2}), encoding='utf-8')
    assert s.watch_stamp(config, folder) == epoch  # Heartbeats never cause repeated HTTP work.
    (folder / 'live-window.json').write_text(json.dumps({'session': 'two', 'tick': 3}), encoding='utf-8')
    assert s.watch_stamp(config, folder) != epoch
    epoch = s.watch_stamp(config, folder)
    # Idle custody status writes must not create an HTTP polling loop.
    (folder / 'status.json').write_text(json.dumps({'at': 123}), encoding='utf-8')
    assert s.watch_stamp(config, folder) == epoch
    def complete_transfer():
        time.sleep(.1)
        (folder / 'lineage.json').write_text(json.dumps({'receipts':['completed-transfer']}), encoding='utf-8')
    producer = threading.Thread(target=complete_transfer)
    producer.start()
    started = time.monotonic()
    s.wait_for_producer(config, folder, False, epoch, 15)
    producer.join()
    assert time.monotonic() - started < 1
print('Sync wake: producer changes and new handoffs are immediate; heartbeat-only updates preserve the network interval.')
