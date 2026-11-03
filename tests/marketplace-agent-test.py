"""Actual PHP HTTP + two isolated save profiles + real crash/recovery boundaries."""
import importlib.util
import http.cookiejar
import json
import os
from pathlib import Path
import socket
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

repo = Path(__file__).resolve().parents[1]
source = repo / "tools/marketplace/custody-agent.py"
spec = importlib.util.spec_from_file_location("save_agent", source)
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


def empty(raw):
    result, at = bytearray(), 0
    for _ in range(7):
        header = bytearray(raw[at:at+68])
        at += struct.unpack_from("<I", raw, at+16)[0]
        struct.pack_into("<I", header, 12, 0)
        struct.pack_into("<I", header, 16, 68)
        struct.pack_into("<H", header, 66, 0)
        result.extend(header)
    return bytes(result)


def transport(endpoint, token, mode, data, action="custody"):
    req = urllib.request.Request(endpoint + f"?action={action}&hardcore={int(mode == 'HC')}",
        None if data is None else json.dumps(data).encode(), {"Authorization": "Bearer " + token, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            raw = response.read()
            try:
                return json.loads(raw)
            except json.JSONDecodeError:
                raise AssertionError('Non-JSON API response: ' + repr(raw[:600])) from None
    except urllib.error.HTTPError as error:
        raise ValueError(f"Marketplace {error.code}: {json.load(error)['error']}") from None


def main():
    php, helper, data, saves = map(lambda p: Path(p).resolve(), sys.argv[1:])
    original = saves / "Stash_SC_Page11.d2i"
    source_hashes = {p: a.io.digest(a.io.read(p)) for p in saves.iterdir() if p.suffix in {".d2i", ".d2s"}}
    inv = a.io.run_helper(helper, data, "inventory", original)
    ring = next(i for i in inv["items"] if i["code"] == "rin")
    price = [dict(kind="gold", code="gld", label="Gold", quality=2, index=-1, ethereal=-1, quantity=600)]
    catalog = a.catalog(data)
    with tempfile.TemporaryDirectory(prefix="market-agent-http-") as temporary:
        root = Path(temporary)
        config = root / "config.php"
        config.write_text("<?php return ['dsn'=>" + json.dumps("sqlite:" + (root / "market.sqlite").as_posix()) + "];", encoding="utf-8")
        env = dict(os.environ, D2_MARKET_CONFIG=str(config), D2_MARKET_LOCAL_TEST="1")
        php_args = [str(php), "-d", "extension_dir=" + str(php.parent / "ext"), "-d", "extension=pdo_sqlite"]
        def cli(*command):
            return json.loads(subprocess.check_output(php_args + [str(repo / "tools/marketplace/server/cli.php"), *command], env=env, text=True))
        cli("init")
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        endpoint = f"http://127.0.0.1:{port}/index.php"
        with (root / "server.log").open("w") as log:
            process = subprocess.Popen(php_args + ["-S", f"127.0.0.1:{port}", "-t", str(repo / "tools/marketplace/server/public")], env=env,
                stdout=log, stderr=log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                for _ in range(100):
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=.1):
                            break
                    except OSError:
                        time.sleep(.05)
                def profile(name, goods=False):
                    actor = cli("user", name)
                    pairing = cli("pair-agent", actor["id"], catalog)
                    folder = root / name / "saves"
                    folder.mkdir(parents=True)
                    a.io.save(folder / "custody-simulation.json", dict(simulation=True))
                    base = empty(original.read_bytes())
                    (folder / "Stash_SC_Page11.d2i").write_bytes(base)
                    (folder / "Stash_SC_Page2.d2i").write_bytes(base)
                    settings = dict(schema=1, game_mode="2.4", mod="RMD-MP", hardcore=False, enabled=True,
                        page=11, withdraw_page=2, auto_trade=False, drafts=[], agreements=[], simulation=dict(enabled=False, page=3, drafts=[]))
                    a.io.save(folder / "Marketplace_2.4_SC.json", settings)
                    cfg = dict(simulation=True, state_directory=str(root / name / "agent"), save_directory=str(folder),
                        mod_data=str(data), helper=str(helper), helper_sha256=a.io.digest(a.io.read(helper)), catalog=catalog,
                        owner=actor["id"], gold_cap=2500000)
                    agent = a.Agent(cfg, lambda mode, packet: transport(endpoint, pairing["token"], mode, packet))
                    if goods:
                        _, after, result = agent.plan("SC", settings, 11, credit_items=[ring])
                        (folder / "Stash_SC_Page11.d2i").write_bytes(after)
                        placed = result["delivered"][0]
                        canonical = agent.inventory(folder / 'Stash_SC_Page11.d2i')['items'][0]
                        # Old live snapshots omitted the seed. Repair the price
                        # only when the saved counterpart is exact and unique.
                        settings["drafts"] = [dict(tab=placed["tab"], item_hex=canonical['legacy_item_hex'], price=price)]
                        a.io.save(folder / "Marketplace_2.4_SC.json", settings)
                    else:
                        raw = bytearray(base)
                        struct.pack_into("<I", raw, 12, 600)
                        (folder / "Stash_SC_Page11.d2i").write_bytes(raw)
                    return actor, pairing, agent
                for point in ("intent", "prepared_remote", "authorized_remote", "page_written", "committed_remote"):
                    # Each crash scenario is an isolated community. Reusing one
                    # real item across accounts in the same ledger is now correctly refused.
                    config.write_text("<?php return ['dsn'=>" + json.dumps("sqlite:" + (root / "market.sqlite").as_posix()) + ",'community'=>" + json.dumps(point) + "];", encoding="utf-8")
                    seller, sp, sa = profile("Seller-" + point, True)
                    buyer, bp, ba = profile("Buyer-" + point)
                    before = sa.page("SC", 11).read_bytes()
                    # Real process termination releases the exclusive Windows lease.
                    launch = root / (point + ".json")
                    a.io.save(launch, dict(config=sa.config, pairing=sp, endpoint=endpoint))
                    code = """import runpy,sys,os,json
t=runpy.run_path(sys.argv[1]); test=runpy.run_path(sys.argv[2]); c=json.load(open(sys.argv[3]))
def fault(stage):
 if stage==sys.argv[4]:os._exit(77)
agent=t['Agent'](c['config'],lambda mode,p:test['transport'](c['endpoint'],c['pairing']['token'],mode,p),fault)
agent.run()
"""
                    child = subprocess.run([sys.executable, "-c", code, str(source), str(Path(__file__).resolve()), str(launch), point], capture_output=True)
                    assert child.returncode == 77, child.stderr.decode(errors="replace")
                    assert sa.pending.exists()
                    sa.run()
                    assert not sa.pending.exists() and sa.inventory(sa.page("SC", 11))["items"] == []
                    ba.run()
                    feed = transport(endpoint, buyer["token"], "SC", None, "sync")["listings"]
                    listing = next(l for l in feed if l["seller_id"] == seller["id"])
                    offer = transport(endpoint, buyer["token"], "SC", dict(id=os.urandom(16).hex(), type="propose", listing_id=listing["id"], listing_revision=int(listing["revision"]), price=price), "command")
                    transport(endpoint, seller["token"], "SC", dict(id=os.urandom(16).hex(), type="accept", offer_id=offer["offer_id"], expected_revision=1), "command")
                    snapshot = transport(endpoint, buyer["token"], "SC", None, "sync")
                    assert snapshot["offers"][0]["status"] == "completed"
                    # Crash in the delivery path, then repeat recovery and a second launch.
                    def fault(stage):
                        if stage == point:
                            raise InterruptedError("simulated process loss")
                    ba.fault = fault
                    try:
                        ba.run(preflight=True)
                    except InterruptedError:
                        pass
                    ba.fault = lambda stage: None
                    ba.run(preflight=True)
                    ba.run(preflight=True)
                    delivered = ba.inventory(ba.page("SC", 2))["items"]
                    assert len(delivered) == 1
                    for key in ("saved_id", "quality", "index", "saved_properties", "socketed_items"):
                        assert delivered[0][key] == ring[key], key
                    sa.run(preflight=True)
                    assert sa.inventory(sa.page("SC", 2))["gold"] == 0  # Unspent wallet funds do not sweep on launch.
                    wallet_request = dict(id=os.urandom(16).hex(), type='wallet_withdraw', balances=price)
                    transport(endpoint, seller['token'], 'SC', wallet_request, 'command')
                    sa.fault = fault
                    try:
                        sa.run(preflight=True)
                    except InterruptedError:
                        pass
                    sa.fault = lambda stage: None
                    sa.run(preflight=True)
                    sa.run(preflight=True)
                    assert sa.inventory(sa.page("SC", 2))["gold"] == 600
                    assert ba.inventory(ba.page("SC", 11))["gold"] == 0
                    assert sa.page("SC", 11).read_bytes() != before
                    # Re-uploading an old discovery snapshot cannot create a ghost listing.
                    item = dict(ring, price=price)
                    item.update(tab=1, item_hex=sa.io_hex if hasattr(sa, "io_hex") else json.loads((sa.root / "transactions" / json.loads((sa.root / "lineage.json").read_text())["receipts"][0] / "record.json").read_text())["request"]["items"][0]["item_hex"])
                    pub = dict(schema=1, game_mode="2.4", mod="RMD-MP", hardcore=False, custody="local_save", purchasable=False,
                        id=os.urandom(16).hex(), captured_at=int(time.time()), expected_revision=0, page=11, enabled=True, source="saved", items=[item])
                    transport(endpoint, seller["token"], "SC", pub, "publish")
                    assert not any(l["seller_id"] == seller["id"] for l in transport(endpoint, buyer["token"], "SC", None, "sync")["listings"])
                    # A different account restoring the same real item is
                    # refused before any save write and can still launch.
                    _, _, clone = profile("Restored-" + point, True)
                    clone_before = clone.page("SC", 11).read_bytes()
                    result = clone.run()
                    assert "already changed owner" in result["SC"]
                    assert not clone.pending.exists() and clone.page("SC", 11).read_bytes() == clone_before
                    clone.run(preflight=True)
                # Website pricing a real unpriced rare uses only the queued
                # sidecar intent, then the same durable deposit protocol.
                config.write_text("<?php return ['dsn'=>" + json.dumps("sqlite:" + (root / "market.sqlite").as_posix()) + ",'community'=>'website-test'];", encoding="utf-8")
                seller, sp, sa = profile("Website-price", True)
                settings = sa.settings("SC")
                settings["drafts"] = []
                a.io.save(sa.folder / "Marketplace_2.4_SC.json", settings)
                sa.run()
                assert len(sa.inventory(sa.page("SC", 11))["items"]) == 1
                opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
                csrf = ""
                def web(action, packet=None):
                    nonlocal csrf
                    req = urllib.request.Request(endpoint.replace("index.php", "web.php") + "?action=" + action,
                        None if packet is None else json.dumps(packet).encode(), {"Content-Type":"application/json", "X-CSRF-Token":csrf})
                    with opener.open(req, timeout=10) as response:
                        result = json.load(response)
                    csrf = result.get("csrf", csrf)
                    return result
                web("snapshot")
                with a.lease(sa.root / "guard.lock"):
                    # Browser linking remains available while a game holds
                    # the separate save lease; it never edits a native save.
                    sa.publish_browser_link()
                link_file = sa.folder / "Marketplace_Web_Link.json"
                first_link = a.io.document(link_file)
                sa.publish_browser_link()
                assert a.io.document(link_file) == first_link
                assert seller["token"] not in link_file.read_text()
                web("link", dict(code=first_link["code"]))
                sa.publish_browser_link()
                assert a.io.document(link_file)["code"] != first_link["code"]
                code = transport(endpoint, seller["token"], "SC", {}, "browser-ticket")["code"]
                web("link", dict(code=code))
                snapshot = web("snapshot")
                local = snapshot["local_items"][0]
                web("command", dict(id=os.urandom(16).hex(), type="web_price", key=local["key"], price=price))
                before = sa.page("SC", 11).read_bytes()
                assert before == sa.page("SC", 11).read_bytes()  # Queuing alone touches no save.
                sa.run()
                snapshot = web("snapshot")
                assert snapshot["local_items"] == [] and snapshot["pending"][0]["status"] == "applied"
                assert len(snapshot["escrow"]["items"]) == 1 and sa.inventory(sa.page("SC", 11))["items"] == []
                display = snapshot["escrow"]["items"][0]["item"]["display"]
                assert display["level"] == ring["level"]
                assert display["saved_properties"] == [{k:p[k] for k in ("id", "param", "value", "list")} for p in ring["saved_properties"]]
                assert [(c["code"],c["quality"],c["index"]) for c in display["socketed_items"]] == [(c["code"],c["quality"],c["index"]) for c in ring["socketed_items"]]
                # A return arriving after the one-shot launch delivery check
                # must be collected by an ordinary background pass, without a
                # new launcher marker or a sweep of unrelated wallet funds.
                escrow_id = snapshot['escrow']['items'][0]['item_id']
                web('command', dict(id=os.urandom(16).hex(), type='escrow_return', item_id=escrow_id))
                raw = bytearray(sa.page('SC', 11).read_bytes())
                struct.pack_into('<I', raw, 12, 300)
                sa.page('SC', 11).write_bytes(raw)
                assert not (sa.root / 'delivery-request.json').exists()
                # A harmless owned Windows process stands in for the running
                # game, with the same authenticated parked-page handoff as HUD.
                live_game = root / 'live-game'
                live_game.mkdir()
                shutil.copy2(Path(os.environ['SystemRoot']) / 'System32/ping.exe', live_game / 'D2R.exe')
                for name in ('d2rhud.dll', 'launcher.exe'):
                    (live_game / name).write_bytes(name.encode())
                child = subprocess.Popen([str(live_game / 'D2R.exe'), '-n', '60', '127.0.0.1'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
                old_processes = a.game_processes
                try:
                    a.game_processes = lambda: {child.pid}
                    live_config = dict(sa.config, simulation=False, enrolled=True, game_directory=str(live_game),
                        launcher_path=str(live_game / 'launcher.exe'), hud_sha256=a.io.digest((live_game / 'd2rhud.dll').read_bytes()),
                        launcher_sha256=a.io.digest((live_game / 'launcher.exe').read_bytes()))
                    grant = dict(schema=2, session='real-item-live-fixture', phase='unloaded', pid=child.pid,
                        started=a.live_process(child.pid)[1], tick=a.ticks(), owner=sa.config['owner'], catalog=sa.config['catalog'],
                        player=17, hardcore=False, deposit=11, delivery=2, parking=1, loaded=11, save_directory=str(sa.folder))
                    a.io.save(sa.root / 'live-window.json', grant)
                    def live_transport(mode, packet):
                        result = sa.transport(mode, packet)
                        grant['tick'] = a.ticks()
                        a.io.save(sa.root / 'live-window.json', grant)
                        return result
                    live = a.Agent(live_config, live_transport)
                    loaded_deposit = sa.page('SC', 11).read_bytes()
                    live.run()
                    assert sa.page('SC', 11).read_bytes() == loaded_deposit
                    assert a.io.run_helper(helper, data, 'inventory', sa.page('SC', 2))['items'][0]['saved_id'] == ring['saved_id']
                    # Now viewing delivery: deposit works, delivery stays untouched.
                    grant.update(loaded=2, tick=a.ticks())
                    a.io.save(sa.root / 'live-window.json', grant)
                    untouched_delivery = sa.page('SC', 2).read_bytes()
                    live.run()
                    assert sa.page('SC', 2).read_bytes() == untouched_delivery
                    assert web('snapshot')['escrow']['balances'][0]['quantity'] == 300
                    withdrawal = dict(id=os.urandom(16).hex(), type='wallet_withdraw', balances=[dict(price[0], quantity=100)])
                    web('command', withdrawal)
                    live.run()  # Never starts a save transaction for the loaded target.
                    assert sa.page('SC', 2).read_bytes() == untouched_delivery and not live.pending.exists()
                    assert next(w for w in web('snapshot')['escrow']['wallet']['withdrawals'] if w['id'] == withdrawal['id'])['status'] == 'queued'
                    grant.update(loaded=11, tick=a.ticks())
                    a.io.save(sa.root / 'live-window.json', grant)
                    loaded_deposit = sa.page('SC', 11).read_bytes()
                    normal_deposit = live.deposit
                    def rejected_deposit(mode):
                        raise ValueError('simulated invalid deposit')
                    live.deposit = rejected_deposit
                    result = live.run()
                    live.deposit = normal_deposit
                    assert result['SC'] == 'simulated invalid deposit'
                    assert sa.page('SC', 11).read_bytes() == loaded_deposit
                    assert a.io.run_helper(helper, data, 'inventory', sa.page('SC', 2))['gold'] == 100
                    assert next(w for w in web('snapshot')['escrow']['wallet']['withdrawals'] if w['id'] == withdrawal['id'])['status'] == 'completed'
                    assert 'SC' not in a.io.document(live.root / 'status.json')['delivery_warnings']
                    # Two legitimate identical deposits reproduce an old page
                    # hash. Receipts remain exactly once; currency is fungible.
                    raw = bytearray(loaded_deposit);struct.pack_into('<I', raw, 12, 50)
                    repeated = bytes(raw)
                    sa.page('SC', 11).write_bytes(repeated)
                    grant.update(loaded=2, tick=a.ticks())
                    a.io.save(sa.root / 'live-window.json', grant)
                    live.run()
                    assert web('snapshot')['escrow']['balances'][0]['quantity'] == 250
                    sa.page('SC', 11).write_bytes(repeated)
                    live.run()
                    assert web('snapshot')['escrow']['balances'][0]['quantity'] == 300
                    live.run()
                    assert web('snapshot')['escrow']['balances'][0]['quantity'] == 300
                    assert child.poll() is None
                    assert a.io.document(sa.root / 'live-result.json')['session'] == grant['session']
                finally:
                    a.game_processes = old_processes
                    child.terminate()
                    child.wait(timeout=5)
                    (sa.root / 'live-window.json').unlink(missing_ok=True)
                returned = sa.inventory(sa.page('SC', 2))['items']
                assert len(returned) == 1 and returned[0]['saved_id'] == ring['saved_id']
                assert returned[0]['saved_properties'] == ring['saved_properties']
                wallet = web('snapshot')['escrow']
                assert wallet['items'] == [] and wallet['balances'][0]['quantity'] == 300
                returned_bytes = sa.page('SC', 2).read_bytes()
                sa.run()
                assert sa.page('SC', 2).read_bytes() == returned_bytes
                # Partial wallet delivery fills only available gold capacity;
                # missing space stays reserved and can be cancelled via browser.
                raw = bytearray(sa.page('SC', 2).read_bytes())
                offset = 0
                for tab in range(7):
                    struct.pack_into('<I', raw, offset + 12, sa.config['gold_cap'] - (100 if tab == 0 else 0))
                    offset += struct.unpack_from('<I', raw, offset + 16)[0]
                sa.page('SC', 2).write_bytes(raw)
                withdraw = dict(id=os.urandom(16).hex(), type='wallet_withdraw', balances=[dict(price[0], quantity=200)])
                web('command', withdraw)
                sa.run()
                request = next(w for w in web('snapshot')['escrow']['wallet']['withdrawals'] if w['id'] == withdraw['id'])
                assert request['status'] == 'queued' and request['remaining'][0]['quantity'] == 100
                assert sa.inventory(sa.page('SC', 2))['gold'] == sa.config['gold_cap'] * 7
                sa.run()
                request = next(w for w in web('snapshot')['escrow']['wallet']['withdrawals'] if w['id'] == withdraw['id'])
                assert request['message'] and not sa.pending.exists()
                web('command', dict(id=os.urandom(16).hex(), type='wallet_cancel', withdrawal_id=request['id'], expected_revision=request['revision']))
                assert web('snapshot')['escrow']['balances'][0]['quantity'] == 200
                # Replaying the original request returns its receipt, not another reservation.
                web('command', withdraw)
                assert web('snapshot')['escrow']['balances'][0]['quantity'] == 200
                # A real Storage Bag: remove balances from a copied deposit,
                # wait without a compatible delivery bag, then credit the same
                # saved fields while preserving its identity and other resources.
                source_bag = next(i for i in inv['items'] if i['code'] == 'Z01' and i['quality'] == 2 and i['balances'] and i['credit_capacity'])
                config.write_text("<?php return ['dsn'=>" + json.dumps("sqlite:" + (root / "market.sqlite").as_posix()) + ",'community'=>'bag-wallet'];", encoding='utf-8')
                bag_owner, bag_pairing, bag_agent = profile('Bag-wallet')
                settings = bag_agent.settings('SC')
                _, placed, _ = bag_agent.plan('SC', settings, 11, credit_items=[source_bag])
                bag_agent.page('SC', 11).write_bytes(placed)
                bag_agent.run()
                deposited = transport(endpoint, bag_owner['token'], 'SC', None, 'sync')['escrow']['balances']
                resource = next(b for b in deposited if b['kind'] == 'bag' and b['quantity'] > 0)
                quantity = min(resource['quantity'], 123)
                payment = dict(kind='bag', code='Z01', label=resource['stat'], stat=resource['stat'], quality=2, index=-1, ethereal=-1, quantity=quantity)
                withdrawal = dict(id=os.urandom(16).hex(), type='wallet_withdraw', balances=[payment])
                transport(endpoint, bag_owner['token'], 'SC', withdrawal, 'command')
                before_delivery = bag_agent.page('SC', 2).read_bytes()
                bag_agent.run()
                assert bag_agent.page('SC', 2).read_bytes() == before_delivery and not bag_agent.pending.exists()
                queued = transport(endpoint, bag_owner['token'], 'SC', None, 'sync')['escrow']['wallet']['withdrawals'][0]
                assert queued['status'] == 'queued' and queued['message']
                empty_bag = next(i for i in bag_agent.inventory(bag_agent.page('SC', 11))['items'] if i['code'] == 'Z01')
                selector = {k:empty_bag[k] for k in ('tab','ordinal','item_hex')}
                _, without_bag, parcel = bag_agent.plan('SC', settings, 11, take=[selector])
                bag_agent.page('SC', 11).write_bytes(without_bag)
                _, delivery_bag, _ = bag_agent.plan('SC', settings, 2, credit_items=parcel['removed']['items'])
                bag_agent.page('SC', 2).write_bytes(delivery_bag)
                bag_agent.run()
                delivered_bag = next(i for i in bag_agent.inventory(bag_agent.page('SC', 2))['items'] if i['code'] == 'Z01')
                assert delivered_bag['saved_id'] == empty_bag['saved_id'] and delivered_bag['item_uuid'] == empty_bag['item_uuid']
                assert next(b['quantity'] for b in delivered_bag['balances'] if b['stat'] == resource['stat']) == quantity
                wallet = transport(endpoint, bag_owner['token'], 'SC', None, 'sync')['escrow']
                assert wallet['wallet']['withdrawals'][0]['status'] == 'completed'
                remaining = next((b['quantity'] for b in wallet['balances'] if b['kind'] == 'bag' and b['stat'] == resource['stat']), 0)
                assert remaining + quantity == resource['quantity']
                final_page = bag_agent.page('SC', 2).read_bytes()
                bag_agent.run();bag_agent.run(preflight=True)
                assert bag_agent.page('SC', 2).read_bytes() == final_page
                # An uncreated deposit page remains repairable in game.
                settings = sa.settings("SC")
                settings["page"] = 64
                a.io.save(sa.folder / "Marketplace_2.4_SC.json", settings)
                sa.run(preflight=True)
                assert all(a.io.digest(a.io.read(p)) == h for p, h in source_hashes.items())
                print("Agent HTTP: two real-item save copies, deposit/settlement/delivery, five interruption boundaries, exactly-once recovery, property/socket conservation, stale discovery suppression original-save immutability and per-page live return and gold delivery while the other trade page stays loaded, loaded-target refusal, explicit wallet withdrawal/recovery, partial capacity waits, cancellation and real bag resource conservation/missing-bag waits and no implicit launch withdrawal passed.")
            finally:
                process.terminate()
                process.wait(timeout=10)


if __name__ == "__main__":
    main()
