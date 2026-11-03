"""Reset touches only trade sidecars, with CAS, pending-journal and mode guards."""
import contextlib
import importlib.util
import json
from pathlib import Path
import tempfile

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('reset_agent', repo / 'tools/marketplace/custody-agent.py')
agent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(agent)

with tempfile.TemporaryDirectory(prefix='market-reset-sidecars-') as directory:
    root = Path(directory)
    a = object.__new__(agent.Agent)
    a.folder = root / 'saves'
    a.root = root / 'state'
    a.folder.mkdir()
    a.root.mkdir()
    a.pending = a.root / 'pending.json'
    a.mod = 'RMD-MP'
    a.save_access = lambda *args: contextlib.nullcontext()
    for mode in ('SC', 'HC'):
        document = dict(schema=1, mod=a.mod, game_mode='2.4', hardcore=mode == 'HC', threads=[{'id': 'old'}], commands=[{'id': 'old'}])
        for stem in ('MarketplaceActivityLocal', 'MarketplaceChatOutbox'):
            agent.io.save(a.folder / f'{stem}_2.4_{mode}.json', document)
    lineage = dict(delivered={'sc': {'mode': 'SC'}, 'hc': {'mode': 'HC'}}, consumed={'sc': {'mode': 'SC'}, 'hc': {'mode': 'HC'}})
    agent.io.save(a.root / 'lineage.json', lineage)
    originals = {}
    for name in ('Stash_SC_Page11.d2i', 'Stash_SC_Page2.d2i', 'Example.d2s'):
        path = a.folder / name
        path.write_bytes(b'game save sentinel: never edit this')
        originals[path] = path.read_bytes()
    a.pending.write_text('{}')
    try:
        a.clear_market_sidecars('SC')
    except BlockingIOError:
        pass
    else:
        raise AssertionError('Pending custody journal ignored')
    a.pending.unlink()
    a.clear_market_sidecars('SC')
    a.clear_market_sidecars('SC')
    for stem in ('MarketplaceActivityLocal', 'MarketplaceChatOutbox'):
        assert not agent.io.document(a.folder / f'{stem}_2.4_SC.json')['commands']
        assert agent.io.document(a.folder / f'{stem}_2.4_HC.json')['commands']
    assert agent.io.document(a.root / 'lineage.json') == dict(delivered={'hc': {'mode': 'HC'}}, consumed={'hc': {'mode': 'HC'}})
    settings_path = a.folder / 'Marketplace_2.4_SC.json'
    agent.io.save(settings_path, dict(schema=1, mod=a.mod, game_mode='2.4', hardcore=False,
        page=11, withdraw_page=2, enabled=True, auto_trade=True, drafts=[], agreements=[{'id':'old'}],
        simulation=dict(enabled=True, drafts=[{'id':'old'}])))
    a.settings = lambda mode: agent.io.document(settings_path)
    a.inventory = lambda path: {'items': []}
    a.page = lambda *args: None
    a.delivery_queue = lambda *args: None
    calls = []
    reset_id = 'a' * 32
    def request(mode, packet):
        calls.append(packet)
        if packet['acknowledged']:
            marker = agent.io.document(a.folder / 'MarketplaceReset_2.4_SC.json')
            assert marker['id'] == reset_id and marker['generation'] == 3
            settings = agent.io.document(settings_path)
            assert not settings['enabled'] and not settings['auto_trade'] and not settings['drafts']
            assert not settings['agreements'] and not settings['simulation']['enabled'] and not settings['simulation']['drafts']
            assert packet['acknowledged'] == [dict(id=reset_id, status='applied', message='')]
            return {'intents': []}
        return {'intents': [dict(id=reset_id, kind='reset', packet={'generation':3})]}
    a.request = request
    a.web_sync('SC')
    assert len(calls) == 2 and calls[-1]['acknowledged']
    assert all(path.read_bytes() == content for path, content in originals.items())
    print('Reset sidecars: pending-journal refusal, idempotent draft/outbox cleanup, other-mode lineage preservation, durable settings/marker before reset acknowledgement and unchanged game saves passed.')
