"""Sync private marketplace sidecars over HTTPS; never open a game save for writing."""
import argparse
from functools import lru_cache
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import time
import uuid
import hashlib

_http_spec = importlib.util.spec_from_file_location('sync_http', Path(__file__).with_name('marketplace-http.py'))
_http = importlib.util.module_from_spec(_http_spec)
_http_spec.loader.exec_module(_http)

_custody_spec = importlib.util.spec_from_file_location('sync_custody_io', Path(__file__).with_name('custody-workspace.py'))
_custody_io = importlib.util.module_from_spec(_custody_spec)
_custody_spec.loader.exec_module(_custody_io)

@lru_cache(maxsize=16)
def presence_value(path, stamp, size):
    if size > 4096:
        return {}
    return json.loads(path.read_text(encoding='utf-8-sig'))


def active_presence(config, folder):
    path = folder / 'MarketplaceChatPresence_2.4.json'
    try:
        stat = path.stat()
        now = time.time()
        if not 0 <= now - stat.st_mtime < 20:
            return False
        presence = presence_value(path, stat.st_mtime_ns, stat.st_size)
        return (isinstance(presence, dict) and presence.get('owner') == config.get('player_id')
                and type(presence.get('at')) is int and 0 <= now - presence['at'] < 15)
    except (OSError, ValueError):
        return False


def watch_stamp(config, folder, hardcore=False):
    """Observe producer changes; do not turn HUD heartbeats into network polls."""
    mode = 'HC' if hardcore else 'SC'
    result = []
    for stem in ('MarketplaceActivityLocal', 'MarketplacePublish', 'MarketplaceTestPublish', 'Marketplace', 'MarketplaceChatOutbox'):
        try:
            stat = (folder / f'{stem}_2.4_{mode}.json').stat()
            result.append((stat.st_mtime_ns, stat.st_size))
        except OSError:
            result.append(None)
    session = None
    receipt = None
    if config.get('custody_status'):
        # The lineage journal changes once per completed transfer. Status and
        # HUD heartbeats change on idle polls, so their mtimes are not signals.
        try:
            stat = (Path(config['custody_status']).parent / 'lineage.json').stat()
            receipt = (stat.st_mtime_ns, stat.st_size)
        except OSError:
            pass
        try:
            raw = (Path(config['custody_status']).parent / 'live-window.json').read_bytes()
            if len(raw) <= 4096:
                session = json.loads(raw).get('session')
        except (OSError, ValueError):
            pass
    return tuple(result) + (session, receipt, active_presence(config, folder))


def wake_custody(owner):
    if os.name != 'nt' or not isinstance(owner, str) or not owner.isalnum():
        return
    import ctypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateEventW.argtypes = (ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_wchar_p)
    kernel.CreateEventW.restype = ctypes.c_void_p
    kernel.SetEvent.argtypes = kernel.CloseHandle.argtypes = (ctypes.c_void_p,)
    event = kernel.CreateEventW(None, False, False, 'Local\\D2RMarketplaceWake-' + owner)
    if event:
        kernel.SetEvent(event)
        kernel.CloseHandle(event)


def config_stamp(path):
    try:
        stat = path.stat()
        return stat.st_mtime_ns, stat.st_size
    except OSError:
        return None


def wait_for_producer(config, folder, hardcore, before, seconds, config_path=None, config_before=None):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if config_path is not None and config_stamp(config_path) != config_before:
            return
        if watch_stamp(config, folder, hardcore) != before:
            return
        time.sleep(min(.25, max(0, deadline - time.monotonic())))


def atomic(path, value):
    raw = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError("Marketplace cache is too large")
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def sync(config, folder, hardcore=False, allow_local=False, refresh_feed=True, poll_inbox=True):
    folder = _custody_io.active_save_directory(folder)
    endpoint = config["endpoint"]
    url = urllib.parse.urlsplit(endpoint)
    if url.username or url.password or url.query or url.fragment:
        raise ValueError("Use a plain endpoint URL without credentials/query/fragment")
    if url.scheme != "https" and not (allow_local and url.scheme == "http" and url.hostname in {"127.0.0.1", "localhost", "::1"}):
        raise ValueError("Marketplace sync requires HTTPS")
    token = os.environ[config.get("token_env", "D2_MARKET_TOKEN")]
    if len(token) != 64 or any(c not in "0123456789abcdef" for c in token):
        raise ValueError("Invalid pairing token")
    mode = "HC" if hardcore else "SC"
    mod = config.get("mod", "RMD-MP")
    # Do not follow redirects: an endpoint redirect must never forward a token
    # to a different host. HTTPS certificate verification is the default.
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *args, **kwargs):
            return None
    opener = urllib.request.build_opener(NoRedirect)

    # Advertise the HUD's versioned interface, independently of its build hash.
    # Signed-runtime fingerprints remain a fallback for previously released HUDs.
    torch_class = False
    try:
        enrollment = json.loads((Path(config['custody_status']).parent.parent / 'custody-enrollment.json').read_text(encoding='utf-8-sig'))
        features = json.loads((Path(config['managed_runtime']) / 'manifest.json').read_text(encoding='utf-8-sig')).get('features', {})
        torch_class = 'torch_class' in _custody_io.hud_features(enrollment, features)
    except (OSError, ValueError, KeyError):
        pass

    def request(action, command=None, bearer=None, extra=None):
        data = None if command is None else _http.encode(command, 8 * 1024 * 1024 if action == "publish" else 32768)
        query = urllib.parse.urlencode({"action": action, "hardcore": int(hardcore), "mod": mod, "version": "2.4", **({"torch_class": 1} if action=="sync" and torch_class else {}), **(extra or {})})
        req = urllib.request.Request(endpoint + "?" + query, data=data, headers={"Authorization": "Bearer " + (token if bearer is None else bearer), "Content-Type": "application/json"})
        network_root = Path(config['custody_status']).parent if config.get('custody_status') else config.get('state_directory')
        return _http.read_json(opener, req, f'{action} ({mode})', state_directory=network_root)

    def validate(snapshot, expected_player=None):
        if snapshot.get("schema") != 1 or snapshot.get("game_mode") != "2.4" or snapshot.get("mod") != mod or snapshot.get("hardcore") is not hardcore:
            raise ValueError("Service belongs to another mod or character mode")
        if snapshot["self"]["id"] != (expected_player or config["player_id"]):
            raise ValueError("Pairing token belongs to a different player")
        for key, limit in (("history", 1000), ("offers", 256), ("listings", 4096)):
            if not isinstance(snapshot.get(key), list) or len(snapshot[key]) > limit:
                raise ValueError("Unsupported service snapshot")
        return snapshot

    cache_path = folder / f"MarketplaceActivity_2.4_{mode}.json"
    state_path = folder / f"MarketplaceSyncState_2.4_{mode}.json"
    state = {"endpoint": endpoint, "player_id": config["player_id"], "results": {}}
    if state_path.exists():
        state = json.loads(state_path.read_text(encoding="utf-8-sig"))
        if state["endpoint"] != endpoint or state["player_id"] != config["player_id"]:
            raise ValueError("Sync state belongs to another service/account")
    snapshot = None
    if not refresh_feed and cache_path.exists():
        try:
            cached = validate(json.loads(cache_path.read_text(encoding='utf-8-sig')))
            if 0 <= time.time() - cached.get('generated_at', 0) < 30:
                snapshot = cached
        except (OSError, ValueError, KeyError):
            pass
    if snapshot is None:
        snapshot = validate(request("sync"))
    control = snapshot.get('market_control', {})
    reset = snapshot.get('market_reset') or {}
    generation = control.get('generation', 0)
    if generation and state.get('generation') != generation:
        state = {'endpoint': endpoint, 'player_id': config['player_id'], 'results': {}, 'generation': generation}
    if control.get('phase') == 'draining' or (reset and not reset.get('acknowledged')):
        message = ('Marketplace reset: returning held items and funds.' if control.get('phase') == 'draining'
                   else 'Marketplace reset: waiting for the updated save companion to clear old trade drafts.')
        snapshot['publication_error'] = message
        snapshot['command_results'] = {}
        atomic(folder / f'MarketplaceActivity_2.4_{mode}.json', snapshot)
        atomic(folder / f'MarketplaceFeed_2.4_{mode}.json', {k: snapshot[k] for k in ('schema', 'game_mode', 'mod', 'hardcore', 'expires_at', 'listings', 'escrow', 'social') if k in snapshot})
        atomic(folder / f'MarketplaceMessages_2.4_{mode}.json', dict(schema=1, game_mode='2.4', mod=mod, hardcore=hardcore, owner=config['player_id'], messages=[], results={}))
        atomic(state_path, state)
        return {'paused': True, 'publication_error': message, 'listings': len(snapshot['listings'])}
    publication_error = []
    def publish_inventory(path, inventory_key=None, bearer=None, player_id=None):
        if not path.exists():
            return None
        try:
            raw = path.read_bytes()
            if len(raw) > 8 * 1024 * 1024:
                raise ValueError("Publication exceeds the service size limit")
            publication = json.loads(raw.decode("utf-8-sig"))
            if publication.get("schema") != 1 or publication.get("game_mode") != "2.4" or publication.get("mod") != mod or publication.get("hardcore") is not hardcore:
                raise ValueError("Publication belongs to another game mode")
            if publication.get("captured_at", 0) < time.time() - 86400:
                raise ValueError("Refresh the deposit page before publishing its expired snapshot")
            settings = json.loads((folder / f"Marketplace_2.4_{mode}.json").read_text(encoding="utf-8-sig"))
            if any(settings.get(k) != publication.get(k) for k in ("schema", "game_mode", "mod", "hardcore")):
                raise ValueError("Refresh the marketplace: publication no longer matches setup")
            inventory = settings[inventory_key] if inventory_key else settings
            if any(inventory.get(k) != publication.get(k) for k in ("page", "enabled")):
                raise ValueError("Refresh the marketplace: publication page changed")
            for item in publication["items"]:
                if not any(d["tab"] == item["tab"] and d["item_hex"] == item["item_hex"] and d["price"] == item["price"] for d in inventory["drafts"]):
                    raise ValueError("Refresh the marketplace: published asking prices changed")
            remote = validate(request("sync", bearer=bearer), player_id) if bearer else snapshot
            fingerprint = hashlib.sha256(raw).hexdigest()
            prior = state.get('publications', {}).get(inventory_key or 'main', {})
            if prior.get('hash') == fingerprint and remote.get('publication', {}).get('id') == publication['id']:
                return dict(prior.get('result', {}), unchanged=True)
            publication["expected_revision"] = remote.get("publication", {}).get("revision", 0)
            result = request("publish", publication, bearer=bearer)
            state.setdefault('publications', {})[inventory_key or 'main'] = dict(hash=fingerprint, result=result)
            atomic(state_path, state)
            if result.get('blocked_identities'):
                publication_error.append('Duplicate or previously transferred item identity refused. No restored copy was hosted.')
            if result.get('unverified_identities'):
                publication_error.append('Close the stash to save and verify item identities before hosting these listings.')
            return result
        except (ValueError, KeyError, TypeError, OSError) as error:
            if isinstance(error, urllib.error.HTTPError):
                if error.code not in {400, 403, 404, 409, 413, 422}:
                    raise
                try:
                    message = json.loads(error.read(4096)).get("error", "Publication refused")
                except ValueError:
                    message = "Publication refused"
            elif isinstance(error, urllib.error.URLError):
                raise
            else:
                message = str(error)
            publication_error.append(("Test seller: " if inventory_key else "") + str(message))
            return None
    published = publish_inventory(folder / f"MarketplacePublish_2.4_{mode}.json")
    test_published = None
    if config.get("test_trader_pairing"):
        test = json.loads(Path(config["test_trader_pairing"]).read_text(encoding="utf-8-sig"))
        if test.get("role") != "tester" or len(test.get("token", "")) != 64:
            raise ValueError("Invalid development trader pairing")
        test_published = publish_inventory(folder / f"MarketplaceTestPublish_2.4_{mode}.json", "simulation", test["token"], test["id"])
    if (published is not None and not published.get('unchanged')) or (test_published is not None and not test_published.get('unchanged')):
        snapshot = validate(request("sync"))
    settings_path = folder / f"Marketplace_2.4_{mode}.json"
    if settings_path.exists():
        preferences = json.loads(settings_path.read_text(encoding="utf-8-sig"))
        if any(preferences.get(k) != v for k, v in {"schema": 1, "game_mode": "2.4", "mod": mod, "hardcore": hardcore}.items()):
            raise ValueError("Preferences belong to another mod or mode")
        approval = bool(preferences.get("enabled", False) and preferences.get("auto_trade", False))
        pending = state.get("preferences")
        if not pending or pending["value"] != approval:
            pending = {"value": approval, "applied": False, "command": {"id": uuid.uuid4().hex, "type": "preferences", "auto_trade": approval}}
            state["preferences"] = pending
            atomic(state_path, state)
        if not pending["applied"]:
            request("command", pending["command"])
            pending["applied"] = True
            atomic(state_path, state)
            snapshot = validate(request("sync"))
    outbox_path = folder / f"MarketplaceActivityLocal_2.4_{mode}.json"
    commands = []
    if outbox_path.exists():
        local = json.loads(outbox_path.read_text(encoding="utf-8-sig"))
        if local.get("mod") != mod or local.get("hardcore") is not hardcore or local.get("game_mode") != "2.4" or local.get("schema") != 1:
            raise ValueError("Outbox belongs to another game mode")
        commands = local["commands"]
        if not isinstance(commands, list) or len(commands) > 4096:
            raise ValueError("Outbox is too large")
    chat_path = folder / f'MarketplaceChatOutbox_2.4_{mode}.json'
    chat_commands = []
    if chat_path.exists():
        raw = chat_path.read_bytes()
        if len(raw) > 2 * 1024 * 1024:
            raise ValueError('Chat outbox is too large')
        chat = json.loads(raw.decode('utf-8-sig'))
        if (chat.get('schema') != 1 or chat.get('owner') != config['player_id'] or chat.get('mod') != mod or
                chat.get('game_mode') != '2.4' or chat.get('hardcore') is not hardcore or
                not isinstance(chat.get('commands'), list) or len(chat['commands']) > 256):
            raise ValueError('Chat outbox belongs to another player or game mode')
        for row in chat['commands']:
            if row.get('type') not in {'direct_message', 'trade_reply'} or type(row.get('hardcore', hardcore)) is not bool:
                raise ValueError('Unsupported chat command')
        chat_commands = chat['commands']
        commands = commands + chat_commands
    changed_commands = False
    if commands:
        for command in commands:
            command_id = command["id"]
            if command_id in state["results"]:
                continue
            changed_commands = True
            try:
                if command.get("type") == "offer_page":
                    for old in commands:
                        if old.get("type") == "offer_page" and old["id"] in state["results"]:
                            state["results"][old["id"]] = {"retired": True}
                state["results"][command_id] = request("command", command, extra={'hardcore': int(command.get('hardcore', hardcore))})
            except urllib.error.HTTPError as error:
                if error.code not in {400, 403, 404, 409, 413, 415, 422}:
                    raise
                raw = error.read(4096)
                try:
                    message = json.loads(raw).get("error", "Command refused")
                except (ValueError, AttributeError):
                    message = "Command refused"
                state["results"][command_id] = {"error": str(message)[:512], "status": error.code}
            atomic(state_path, state)  # A crash/retry reuses the exact idempotency key.
        if changed_commands:
            snapshot = validate(request("sync"))
    if poll_inbox:
        # Durable incoming messages are separate from trade acceptance and the save
        # journal. Stop fetching if the local unprinted buffer fills while offline.
        seen = 0
        try:
            receipt = json.loads((folder / 'MarketplaceChatSeen_2.4.json').read_text(encoding='utf-8-sig'))
            if receipt.get('owner') == config['player_id'] and type(receipt.get('through')) is int:
                seen = max(0, receipt['through'])
        except (OSError, ValueError):
            pass
        messages = [m for m in state.get('inbox_messages', []) if m['seq'] > seen]
        cursor = state.get('inbox_cursor', 0)
        inbox = request('inbox', extra={'after': cursor, 'notification_filters': 1, 'preferences_only': int(len(messages) > 156)})
        rows = inbox.get('messages')
        if inbox.get('owner') != config['player_id'] or not isinstance(rows, list) or len(rows) > 100:
            raise ValueError('Incoming messages belong to another player')
        last = cursor
        for row in rows:
            if (type(row.get('seq')) is not int or row['seq'] <= last or row.get('recipient') != config['player_id'] or
                    row.get('kind') not in {'direct','trade'} or not isinstance(row.get('body'), str) or len(row['body'].encode('utf-8')) > 4096):
                raise ValueError('Invalid incoming message cache')
            last = row['seq']
        next_cursor = inbox.get('next', last)
        if type(next_cursor) is not int or next_cursor < last:
            raise ValueError('Invalid incoming message cursor')
        preferences = inbox.get('delivery_preferences', {})
        muted = inbox.get('muted_senders', [])
        if (not isinstance(preferences, dict) or any(type(v) is not bool for v in preferences.values()) or
                not isinstance(muted, list) or len(muted) > 4096 or any(not isinstance(v, str) or len(v) > 64 for v in muted)):
            raise ValueError('Invalid incoming message preferences')
        muted = set(muted)
        def deliver(row):
            # Also handle queued notices written by companions predating categories.
            category = 'search' if row.get('body', '').startswith('Saved search ') else row.get('category', row['kind'])
            if category not in {'direct', 'trade', 'search'}:
                category = row['kind']
            row['category'] = category
            return row.get('sender') not in muted and preferences.get('game_' + category, category != 'search')
        messages = [row for row in messages if deliver(row)]
        incoming = [row for row in rows if deliver(row)]
        if len(messages) + len(incoming) <= 256:
            messages += incoming
        else:
            next_cursor = cursor
        # A filtered batch can contain zero rows. Advance over that batch so
        # muted notifications cannot starve newer messages indefinitely.
        state['inbox_cursor'] = next_cursor
        state['inbox_unread'] = inbox.get('unread', 0)
        state['inbox_messages'] = messages
        atomic(folder / f'MarketplaceMessages_2.4_{mode}.json', dict(schema=1, game_mode='2.4', mod=mod, hardcore=hardcore,
            owner=config['player_id'], messages=messages, results={r['id']:state['results'][r['id']] for r in chat_commands if r['id'] in state['results']}))
    snapshot["command_results"] = state["results"]
    if config.get("custody_status") and Path(config["custody_status"]).exists():
        custody = json.loads(Path(config["custody_status"]).read_text(encoding="utf-8-sig"))
        if isinstance(custody.get('error'), str) and custody['error']:
            publication_error.append('Marketplace paused: ' + custody['error'][:480])
        warning = custody.get("delivery_warnings", {}).get(mode)
        if isinstance(warning, str):
            publication_error.append(warning[:512])
        deposit_warning = custody.get('result', {}).get(mode)
        if isinstance(deposit_warning, str):
            publication_error.append(deposit_warning[:512])
    snapshot["publication_error"] = "; ".join(publication_error)[:512]
    custody_stamp = hashlib.sha256(json.dumps([snapshot.get('escrow'),
        [(o.get('id'),o.get('revision'),o.get('status')) for o in snapshot.get('offers',[])],
        snapshot.get('market_reset'),snapshot.get('market_control')],sort_keys=True).encode()).hexdigest()
    custody_changed = state.get('custody_stamp') != custody_stamp or changed_commands or published is not None and not published.get('unchanged')
    state['custody_stamp'] = custody_stamp
    atomic(cache_path, snapshot)
    feed = {k: snapshot[k] for k in ("schema", "game_mode", "mod", "hardcore", "expires_at", "listings")}
    if "social" in snapshot:
        feed["social"] = snapshot["social"]
    if "escrow" in snapshot:
        feed["escrow"] = snapshot["escrow"]
    atomic(folder / f"MarketplaceFeed_2.4_{mode}.json", feed)
    atomic(state_path, state)
    result = {"offers": len(snapshot["offers"]), "recorded_sales": len(snapshot["history"]), "listings": len(snapshot["listings"])}
    result['custody_changed'] = bool(custody_changed)
    if published is not None:
        result["published"] = published["published"]
    if test_published is not None:
        result["test_published"] = test_published["published"]
    if publication_error:
        result["publication_error"] = "; ".join(publication_error)[:512]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("save_folder", type=Path)
    parser.add_argument("--hardcore", action="store_true")
    parser.add_argument("--watch", action="store_true", help="Keep the development sync companion running until stopped")
    parser.add_argument("--interval", type=int, default=15)
    args = parser.parse_args()
    if not 5 <= args.interval <= 300:
        parser.error("Use an interval from 5 to 300 seconds")
    try:
        last_feed, last_input_stamp = 0, None
        while True:
            config = {}
            retry_soon = False
            before = watch_stamp(config, args.save_folder, args.hardcore)
            config_before = config_stamp(args.config)
            try:
                config = json.loads(args.config.read_text(encoding="utf-8-sig"))
                if args.watch and config.get('managed_runtime') and Path(config['managed_runtime']).resolve() != Path(__file__).resolve().parent:
                    break
                if args.watch and config.get('save_directory') and Path(config['save_directory']).resolve() != args.save_folder.resolve():
                    break  # The supervisor restarts us with the current live route.
                before = watch_stamp(config, args.save_folder, args.hardcore)
                refresh_feed = before != last_input_stamp or time.monotonic() - last_feed >= 20
                result = sync(config, args.save_folder, args.hardcore, refresh_feed=refresh_feed, poll_inbox=before[-1])
                if refresh_feed: last_feed = time.monotonic()
                last_input_stamp = watch_stamp(config, args.save_folder, args.hardcore)
                if result.get('custody_changed'): wake_custody(config.get('player_id'))
                print(json.dumps(result), flush=True)
            except (_http.LocalRequestPending, _http.ReadDeferred):
                if not args.watch:
                    raise
                retry_soon = True
            except (ValueError, OSError) as error:
                if not args.watch:
                    raise
                print(json.dumps({"sync_error": str(error)[:512]}), flush=True)
            if not args.watch:
                break
            if not config:
                before = watch_stamp(config, args.save_folder, args.hardcore)
            # The custody worker still checks delivery queues while the game
            # is closed. Only the HUD's feed/chat worker needs this slower cadence.
            interval = (5 if before[-1] else max(120, args.interval)) * _http.random.uniform(1, 1.2)
            if retry_soon:
                interval = min(interval, 5 * _http.random.uniform(1, 1.2))
            wait_for_producer(config, args.save_folder, args.hardcore, before, interval, args.config, config_before)
    except KeyboardInterrupt:
        pass
