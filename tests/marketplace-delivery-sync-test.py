"""A reload ACK must never permanently suppress an undelivered queue."""
import contextlib, importlib.util, json, tempfile
from pathlib import Path
from unittest.mock import patch
repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('delivery_agent',repo/'tools/marketplace/custody-agent.py')
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
with tempfile.TemporaryDirectory(prefix='delivery-retry-') as tmp:
    root=Path(tmp);agent=a.Agent.__new__(a.Agent);agent.root=root;agent.manual_sync=set()
    window=dict(refresh_supported=2,loaded=11,delivery=11,refresh_id='',owner='fixture',catalog='a'*64,pid=42,started=123,player=1,hardcore=False,session='session-one')
    @contextlib.contextmanager
    def access(*args,**kwargs):yield window
    agent.save_access=access;reason='items:'+'b'*32
    clock=[1000]
    (root/'pending.json').write_text('{"id":"durable-receipt"}');sentinel=root/'stash.d2i';sentinel.write_bytes(b'unchanged')
    with patch.object(a.time,'time',side_effect=lambda:clock[0]),patch.object(a,'ticks',side_effect=lambda:int(clock[0]*1000)):
        assert agent.request_refresh('SC',11,reason)
        old=a.io.document(root/'live-refresh-request.json');window['refresh_id']=old['id']
        agent.refresh_ready(window);window['refresh_id']=''
        assert not agent.request_refresh('SC',11,reason),'Reload repeats continuously'
        clock[0]+=31
        assert agent.request_refresh('SC',11,reason),'Undelivered item permanently suppressed'
        current=a.io.document(root/'live-refresh-request.json');assert current['id']!=old['id']
        agent.manual_sync.add('SC');clock[0]+=1
        # An existing fresh native request is reused; never clobber a handoff.
        assert agent.request_refresh('SC',11,reason)
        assert a.io.document(root/'live-refresh-request.json')['id']==current['id']
        window['refresh_id']=current['id'];agent.refresh_ready(window);window['refresh_id']=''
        assert agent.request_refresh('SC',11,reason),'Manual retry ignored'
        assert a.io.document(root/'live-refresh-request.json')['id']!=current['id'],'Acknowledged reload was incorrectly reused'
        gold=dict(current,kind='gold_deposit');gold.pop('key');gold.pop('trigger');a.io.save(root/'live-refresh-request.json',gold)
        window['refresh_id']=gold['id'];(root/'live-refresh-result.json').unlink();agent.refresh_ready(window);window['refresh_id']=''
        window['loaded']=2;assert not agent.request_refresh('SC',11,reason),'Reloaded a page that is not viewed'
        assert not agent.request_refresh('SC',2,'force:anything'),'Arbitrary refresh bypass accepted'
    assert sentinel.read_bytes()==b'unchanged' and (root/'pending.json').read_text()=='{"id":"durable-receipt"}'
print('PASS delivery retry: bounded automatic retry after ACK, fresh manual retry, active handoff reuse, native gold ACK, loaded-page checks and untouched save/receipt.')
