"""Slow optional website publishing cannot hold the custody loop."""
from pathlib import Path
import importlib.util
import json
import sys
import tempfile
import threading
from unittest.mock import patch

repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('agent',repo/'tools/marketplace/custody-agent.py')
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
started,release,finished=threading.Event(),threading.Event(),threading.Event()
runs=[]
class Done(Exception): pass
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);cfg=dict(owner='fixture',state_directory=str(root),catalog='fixture')
    config=root/'config.json';config.write_text(json.dumps(cfg))
    class FakeAgent:
        def __init__(self,config): self.config=config;self.root=root
        def refresh_active(self): return False
        def run(self,*args,**kwargs):
            runs.append(len(runs)+1)
            return {'SC':0}
        def publish_browser_link(self):
            started.set();release.wait(5);finished.set()
        def publish_characters(self): pass
    class Wake:
        def __init__(self,*args): pass
        def wait(self,*args):
            if len(runs)==1: assert started.wait(2)
            if len(runs)>=2:
                assert not finished.is_set(), 'custody was blocked until optional networking finished'
                raise Done()
    try:
        with patch.object(a,'Agent',FakeAgent),patch.object(a,'WakeEvent',Wake),patch.object(sys,'argv',['agent',str(config),'--watch']):
            try:a.main()
            except Done:pass
        assert len(runs)==2
    finally:
        release.set();assert finished.wait(2)
print('Custody continues while optional browser/profile publication is blocked; only one optional worker runs.')
