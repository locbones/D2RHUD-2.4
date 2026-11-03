"""Local HTTP integration against the actual PHP API and Python sync companion."""
import hashlib
import importlib.util
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
php = Path(sys.argv[1]).resolve()
server = repo / "tools/marketplace/server"
spec = importlib.util.spec_from_file_location("market_sync", repo / "tools/marketplace/sync-marketplace.py")
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)
with tempfile.TemporaryDirectory(prefix="d2-market-http-") as directory:
    root = Path(directory)
    config = root / "config.php"
    config.write_text("<?php return ['dsn'=>" + json.dumps("sqlite:" + (root / "market.sqlite").as_posix()) + "];", encoding="utf-8")
    env = dict(os.environ, D2_MARKET_CONFIG=str(config), D2_MARKET_LOCAL_TEST="1")
    args = [str(php), "-d", "extension_dir=" + str(php.parent / "ext"), "-d", "extension=pdo_sqlite"]
    def cli(*command):
        return json.loads(subprocess.check_output(args + [str(server / "cli.php"), *command], env=env, text=True))
    cli("init")
    seller, buyer, other = [cli("user", name) for name in ("Seller", "Buyer", "Other")]
    gold = [{"code": "gld", "label": "Gold", "quality": 2, "index": -1, "ethereal": -1, "quantity": 600, "kind": "gold"}]
    listing_file = root / "listing.json"
    listing_file.write_text(json.dumps({"owner": seller["id"], "price": gold, "item": {"seed": 305419896, "code": "rin", "quality": 7, "index": 42, "ethereal": 0, "sockets": 0, "item_hex": "abcd"}}))
    listing = cli("import-listing", str(listing_file))
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    endpoint = f"http://127.0.0.1:{port}/index.php"
    with (root / "server.log").open("w") as log:
        process = subprocess.Popen(args + ["-S", f"127.0.0.1:{port}", "-t", str(server / "public")], env=env, stdout=log, stderr=log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            for _ in range(100):
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=.1):
                        break
                except OSError:
                    if process.poll() is not None:
                        raise RuntimeError("PHP server failed to start")
                    time.sleep(.05)
            def api(actor, command=None, history=None):
                url = endpoint + "?action=" + ("history" if history is not None else "sync" if command is None else "command") + "&hardcore=0"
                if history is not None:
                    url += "&" + urllib.parse.urlencode(history)
                req = urllib.request.Request(url, None if command is None else json.dumps(command).encode(), {"Authorization": "Bearer " + actor["token"], "Content-Type": "application/json"})
                with urllib.request.urlopen(req, timeout=10) as result:
                    return json.load(result)
            os.environ["D2_MARKET_TEST_TOKEN"] = buyer["token"]
            settings = {"endpoint": endpoint, "player_id": buyer["id"], "token_env": "D2_MARKET_TEST_TOKEN", "mod": "RMD-MP"}
            save = root / "Stash_SC_Page11.d2i"
            save.write_bytes(b"read-only game save sentinel")
            before = hashlib.sha256(save.read_bytes()).digest()
            outbox = {"schema": 1, "game_mode": "2.4", "mod": "RMD-MP", "hardcore": False, "threads": [], "commands": [{"id": "http-offer", "type": "propose", "listing_id": listing["listing_id"], "listing_revision": 1, "price": gold, "message": "A real API integration test message"}]}
            (root / "MarketplaceActivityLocal_2.4_SC.json").write_text(json.dumps(outbox))
            assert client.sync(settings, root, allow_local=True)["offers"] == 1
            assert client.sync(settings, root, allow_local=True)["offers"] == 1
            custody_status = root / 'custody-status.json'
            settings['custody_status'] = str(custody_status)
            custody_status.write_text(json.dumps({'error':'The guarded launcher or HUD changed; revalidate custody before writing saves'}))
            blocked = client.sync(settings, root, allow_local=True)
            assert 'revalidate custody' in blocked['publication_error']
            assert 'revalidate custody' in json.loads((root / 'MarketplaceActivity_2.4_SC.json').read_text())['publication_error']
            custody_status.write_text(json.dumps({'delivery_warnings':{}}))
            assert not client.sync(settings, root, allow_local=True).get('publication_error')
            assert len(api(buyer)["offers"][0]["events"]) == 1
            assert api(other)["offers"] == []
            api(seller, {"id": "http-accept", "type": "accept", "offer_id": "http-offer", "expected_revision": 1})
            assert api(buyer)["history"] == []
            client.sync(settings, root, allow_local=True)
            cached = json.loads((root / "MarketplaceActivity_2.4_SC.json").read_text())
            assert cached["offers"][0]["status"] == "accepted" and cached["command_results"]["http-offer"]["offer_id"] == "http-offer"
            sale = root / "sale.json"
            sale.write_text(json.dumps({"item_id": listing["item_id"], "seller": seller["id"], "buyer": buyer["id"], "price": gold, "receipt": "http-completed-receipt"}))
            cli("record-sale", str(sale))
            assert client.sync(settings, root, allow_local=True)["recorded_sales"] == 1
            assert len(api(buyer, history={"item_id": listing["item_id"]})["rows"]) == 1
            assert len(api(buyer, history={"code": "rin", "seed": 305419896})["rows"]) == 1
            assert len(api(buyer, history={"code": "rin", "similar": 1, "quality": 7, "item_index": 42, "ethereal": 0, "sockets": 0})["rows"]) == 1
            native_settings = {"schema": 1, "game_mode": "2.4", "mod": "RMD-MP", "hardcore": False, "enabled": True, "page": 11, "drafts": [{"tab": 3, "item_hex": "bcad", "price": gold}], "simulation": {"enabled": True, "page": 3, "drafts": [{"tab": 2, "item_hex": "abce", "price": gold}]}}
            publication = {k: native_settings[k] for k in ("schema", "game_mode", "mod", "hardcore", "enabled", "page")}
            publication.update({"id": "native-publication", "captured_at": int(time.time()), "custody": "local_save", "purchasable": False, "source": "live", "items": [{"tab": 3, "item_hex": "bcad", "code": "rin", "saved_id": 1234, "quality": 7, "index": 42, "ethereal": False, "socketed_items": [], "price": gold}]})
            (root / "Marketplace_2.4_SC.json").write_text(json.dumps(native_settings))
            (root / "MarketplacePublish_2.4_SC.json").write_text(json.dumps(publication))
            assert client.sync(settings, root, allow_local=True)["published"] == 1
            published_id = api(seller)["listings"][0]["id"]
            assert client.sync(settings, root, allow_local=True)["published"] == 1
            assert api(seller)["listings"][0]["id"] == published_id
            publication["items"][0]["price"] = [dict(gold[0], quantity=700)]
            publication["id"] = "repriced-publication"
            (root / "MarketplacePublish_2.4_SC.json").write_text(json.dumps(publication))
            assert "publication_error" in client.sync(settings, root, allow_local=True)
            assert api(seller)["listings"][0]["price"] == gold
            native_settings["drafts"][0]["price"] = publication["items"][0]["price"]
            (root / "Marketplace_2.4_SC.json").write_text(json.dumps(native_settings))
            assert client.sync(settings, root, allow_local=True)["published"] == 1
            assert api(seller)["listings"][0]["revision"] == "2"
            trader = cli("user", "Development trader", "tester")
            trader_file = root / "private-trader.json"
            trader_file.write_text(json.dumps(trader))
            settings["test_trader_pairing"] = str(trader_file)
            test_publication = dict(publication, id="test-seller-publication", page=3, items=[dict(publication["items"][0], tab=2, item_hex="abce", price=gold)])
            (root / "MarketplaceTestPublish_2.4_SC.json").write_text(json.dumps(test_publication))
            result = client.sync(settings, root, allow_local=True)
            assert result["published"] == 1 and result["test_published"] == 1
            cached_feed = json.loads((root / "MarketplaceFeed_2.4_SC.json").read_text())
            dev = [r for r in cached_feed["listings"] if r["development"]]
            assert len(dev) == 1 and dev[0]["seller_id"] == trader["id"] and not dev[0]["own"] and dev[0]["tab"] == 2
            assert trader["token"] not in (root / "MarketplaceFeed_2.4_SC.json").read_text()
            trader_spec = importlib.util.spec_from_file_location("test_trader", repo / "tools/marketplace/test-trader.py")
            trader_module = importlib.util.module_from_spec(trader_spec)
            trader_spec.loader.exec_module(trader_module)
            bot = trader_module.Trader(trader, endpoint, allow_local=True)
            proposal = bot.propose(published_id)
            incoming = [r for r in api(buyer)["offers"] if r["id"] == proposal["offer_id"]][0]
            assert incoming["development"] and incoming["buyer"] == trader["id"]
            api(buyer, {"id": "manual-buyer-counter", "type": "counter", "offer_id": incoming["id"], "expected_revision": 1, "price": gold, "message": "Manually chosen test terms"})
            bot.respond(incoming["id"], "message", message="Development message through the real service")
            bot.respond(incoming["id"], "reject", message="Test decision")
            final_thread = [r for r in api(buyer)["offers"] if r["id"] == incoming["id"]][0]
            assert final_thread["status"] == "rejected" and len(final_thread["events"]) == 4
            assert len(api(buyer)["history"]) == 1  # The development negotiation added no completed sale.
            # The HUD uses the same command outbox for identity, favorites and paged offers.
            import uuid
            rename, favorite, page1, page2 = [uuid.uuid4().hex for _ in range(4)]
            outbox['commands'] += [{'id':rename,'type':'profile_name','name':'Buyer Renamed','expected_name':'Buyer'},
                {'id':favorite,'type':'favorite','kind':'listing','target':published_id,'enabled':True},
                {'id':page1,'type':'offer_page','listing_id':published_id}]
            (root / 'MarketplaceActivityLocal_2.4_SC.json').write_text(json.dumps(outbox))
            client.sync(settings, root, allow_local=True)
            cached = json.loads((root / 'MarketplaceActivity_2.4_SC.json').read_text())
            feed = json.loads((root / 'MarketplaceFeed_2.4_SC.json').read_text())
            assert cached['self']['name'] == 'Buyer Renamed' and cached['self']['id'] == buyer['id']
            assert {'kind':'listing','target':published_id} in feed['social']['favorites']
            assert cached['command_results'][page1]['total'] == 1
            assert cached['command_results'][page1]['rows'][0]['alias'].startswith('Trader ')
            outbox['commands'].append({'id':page2,'type':'offer_page','listing_id':published_id})
            (root / 'MarketplaceActivityLocal_2.4_SC.json').write_text(json.dumps(outbox))
            client.sync(settings, root, allow_local=True)
            cached = json.loads((root / 'MarketplaceActivity_2.4_SC.json').read_text())
            assert cached['command_results'][page1] == {'retired':True} and cached['command_results'][page2]['total'] == 1
            publication.update(id="withdraw-publication", items=[])
            native_settings["drafts"] = []
            (root / "Marketplace_2.4_SC.json").write_text(json.dumps(native_settings))
            (root / "MarketplacePublish_2.4_SC.json").write_text(json.dumps(publication))
            assert client.sync(settings, root, allow_local=True)["published"] == 0
            assert all(r["seller_id"] != buyer["id"] for r in api(seller)["listings"])
            assert hashlib.sha256(save.read_bytes()).digest() == before
            wrong = dict(settings, player_id=other["id"])
            try:
                client.sync(wrong, root, allow_local=True)
            except ValueError:
                pass
            else:
                raise AssertionError("Accepted a token for a different player")
            try:
                client.sync(settings, root)
            except ValueError:
                pass
            else:
                raise AssertionError("HTTP accepted outside explicit loopback testing")
        finally:
            process.terminate()
            process.wait(timeout=15)
            os.environ.pop("D2_MARKET_TEST_TOKEN", None)
print("HTTP sync: real PHP API, private offers/messages, idempotent retries, accepted-vs-completed sales, account pinning, HTTPS policy and untouched game saves passed.")
