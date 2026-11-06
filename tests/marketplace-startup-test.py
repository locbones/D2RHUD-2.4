"""Startup scheduling without a game, network, credentials or save writes."""
import importlib.util
import json
from pathlib import Path
import tempfile
import threading

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('startup_agent', repo / 'tools/marketplace/custody-agent.py')
agent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(agent)


def main():
    import sys
    class EndCycle(Exception): pass
    with tempfile.TemporaryDirectory(prefix='market-startup-') as folder:
        root = Path(folder)
        config = root / 'config.json'
        config.write_text(json.dumps({'state_directory': str(root), 'owner': 'fixture'}), encoding='utf-8')
        calls = []
        complete = threading.Event()
        class Publisher:
            def __init__(self, cfg): self.root = Path(cfg['state_directory'])
            def bootstrap(self): calls.append('bootstrap')
            def run(self, preflight):
                calls.append('waiting')
                raise BlockingIOError('No native handoff yet')
            def refresh_active(self): return False
            def publish_browser_link(self): calls.append('link')
            def publish_characters(self): calls.append('characters'); complete.set()
        class Wake:
            def __init__(self, owner): pass
            def wait(self, seconds):
                assert complete.wait(3), 'Linking waited for a native stash handoff'
                raise EndCycle()
        agent.Agent, agent.WakeEvent = Publisher, Wake
        sys.argv = ['custody-agent.py', str(config), '--watch']
        try: agent.main()
        except EndCycle: pass
        assert calls == ['bootstrap', 'waiting', 'link', 'characters'], calls
        status = json.loads((root / 'status.json').read_text())
        assert status['phase'] == 'waiting_for_stash' and status['error'] == ''
        assert sorted(p.name for p in root.iterdir()) == ['config.json', 'status.json']
    print('PASS: first-time browser linking and character publishing continue while native stash access waits; no save writes.')


if __name__ == '__main__': main()
