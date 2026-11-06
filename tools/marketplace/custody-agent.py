"""Guarded Vault agent. Requires enrollment and the launcher/HUD file leases.

The simulation-only copy tool remains separate. This agent journals a cloud
ticket BEFORE contacting the service and obtains write authorization BEFORE
touching a page. Live writes require native confirmation that trade pages are
unloaded. Transfers never gate game startup.
"""
import argparse
from contextlib import contextmanager, ExitStack
import ctypes
import importlib.util
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import time
import threading
import urllib.error
import urllib.parse
import urllib.request
import uuid

spec = importlib.util.spec_from_file_location("custody_io", Path(__file__).with_name("custody-workspace.py"))
io = importlib.util.module_from_spec(spec)
spec.loader.exec_module(io)


_RUNTIME_FEATURES = None

def web_features(config):
    global _RUNTIME_FEATURES
    if _RUNTIME_FEATURES is None:
        try:
            _RUNTIME_FEATURES = io.document(Path(__file__).with_name('manifest.json')).get('features', {})
        except (OSError, ValueError):
            _RUNTIME_FEATURES = {}
    return io.hud_features(config, _RUNTIME_FEATURES)


_RUNTIME_VERSION = None

def companion_version():
    global _RUNTIME_VERSION
    if _RUNTIME_VERSION is None:
        try:
            version = io.document(Path(__file__).with_name('manifest.json')).get('version', '')
            _RUNTIME_VERSION = version if re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version) else '0.0.0'
        except (OSError, ValueError, TypeError):
            _RUNTIME_VERSION = '0.0.0'
    return _RUNTIME_VERSION


def catalog(data):
    return io.digest(json.dumps(io.compatibility(data), sort_keys=True, separators=(",", ":")).encode())


def positionless(value):
    raw = bytearray.fromhex(value)
    for bit in range(35, 53):
        raw[bit // 8] &= ~(1 << (bit % 8))
    return raw.hex()


@contextmanager
def lease(path):
    """Same Windows share-mode lease as Pandemonium and the injected HUD."""
    if os.name != "nt":
        with Path(path).open("a+b") as stream:
            import fcntl
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            yield
        return
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = (ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                       ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p)
    create.restype = ctypes.c_void_p
    handle = create(str(Path(path).resolve()), 0xC0000000, 0, None, 4, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        yield
    finally:
        kernel.CloseHandle.argtypes = (ctypes.c_void_p,)
        kernel.CloseHandle(handle)


def game_processes():
    if os.name != "nt":
        return set()
    # Local process snapshot: no subprocess, network call or tasklist timeout
    # inside the short save-write gate. Never attach to or terminate a game.
    from ctypes import wintypes
    class Entry(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                    ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                    ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG),
                    ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    snapshot = kernel.CreateToolhelp32Snapshot
    snapshot.argtypes = (wintypes.DWORD, wintypes.DWORD)
    snapshot.restype = ctypes.c_void_p
    handle = snapshot(2, 0)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    first, next_entry = kernel.Process32FirstW, kernel.Process32NextW
    for method in (first, next_entry):
        method.argtypes = (ctypes.c_void_p, ctypes.POINTER(Entry))
        method.restype = wintypes.BOOL
    try:
        entry = Entry()
        entry.dwSize = ctypes.sizeof(Entry)
        present = first(handle, ctypes.byref(entry))
        processes = set()
        while present:
            if entry.szExeFile.lower() in {"d2r.exe", "d2r_debug.exe", "d2r_debug_noerrors.exe"}:
                processes.add(entry.th32ProcessID)
            present = next_entry(handle, ctypes.byref(entry))
        if ctypes.get_last_error() != 18:  # ERROR_NO_MORE_FILES
            raise ctypes.WinError(ctypes.get_last_error())
        return processes
    finally:
        kernel.CloseHandle.argtypes = (ctypes.c_void_p,)
        kernel.CloseHandle(handle)


def game_running():
    return bool(game_processes())


def live_process(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    opened = kernel.OpenProcess
    opened.argtypes = (ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32)
    opened.restype = ctypes.c_void_p
    handle = opened(0x1000, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        query = kernel.QueryFullProcessImageNameW
        query.argtypes = (ctypes.c_void_p, ctypes.c_uint32, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint32))
        size = ctypes.c_uint32(32768)
        name = ctypes.create_unicode_buffer(size.value)
        if not query(handle, 0, name, ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        times = [ctypes.c_uint64() for _ in range(4)]
        method = kernel.GetProcessTimes
        method.argtypes = (ctypes.c_void_p, *([ctypes.POINTER(ctypes.c_uint64)] * 4))
        if not method(handle, *(ctypes.byref(t) for t in times)):
            raise ctypes.WinError(ctypes.get_last_error())
        return Path(name.value).resolve(), times[0].value
    finally:
        kernel.CloseHandle.argtypes = (ctypes.c_void_p,)
        kernel.CloseHandle(handle)


def ticks():
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetTickCount64.restype = ctypes.c_uint64
    return kernel.GetTickCount64()


class WakeEvent:
    def __init__(self, owner):
        self.handle = None
        if os.name == 'nt':
            self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            create = self.kernel.CreateEventW
            create.argtypes = (ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_wchar_p)
            create.restype = ctypes.c_void_p
            self.handle = create(None, False, False, 'Local\\D2RMarketplaceWake-' + owner)
    def wait(self, seconds):
        if self.handle:
            wait = self.kernel.WaitForSingleObject
            wait.argtypes = (ctypes.c_void_p, ctypes.c_uint32)
            wait(self.handle, int(seconds * 1000))
        else:
            time.sleep(seconds)


class CapacityError(ValueError):
    def __init__(self, message, shrink=True):
        super().__init__(message)
        self.shrink = shrink


class DepositRejected(ValueError):
    """The server refused admission, with no ticket and no native save write."""


class RepairInProgress(BlockingIOError):
    """A launcher repair owns the scheduling gate; this is not a sync failure."""


class Agent:
    def __init__(self, config, transport=None, fault=None):
        self.config = config
        self.root = Path(config["state_directory"]).resolve()
        self.folder = Path(config["save_directory"]).resolve()
        self.data = Path(config["mod_data"]).resolve()
        self.mod = io.mod_name(self.data)
        if config.get('mod', self.mod) != self.mod or config.get('game_mode', '2.4') != '2.4':
            raise ValueError('The enrollment belongs to another mod or game version')
        self.helper = Path(config["helper"]).resolve()
        self.test = config.get("simulation") is True
        if self.test:
            if "diablo ii resurrected" in str(self.folder).lower() or not (self.folder / "custody-simulation.json").exists():
                raise ValueError("Test agents require a marked, isolated save copy")
        elif os.name != "nt" or config.get("enrolled") is not True:
            raise ValueError("Enroll the Windows launch guard before enabling custody")
        if self.root == self.folder or self.root in self.folder.parents or self.folder in self.root.parents:
            raise ValueError("Keep custody recovery records outside the save folder")
        self.root.mkdir(parents=True, exist_ok=True)
        (self.root / "transactions").mkdir(exist_ok=True)
        self.pending = self.root / "pending.json"
        self.game_lease = self.root.parent / "game.lock"
        self.delivery_items = {}
        self.wallet_withdrawals = {}
        self.funding_requests = {}
        self.transport = transport
        self.fault = fault or (lambda stage: None)
        self.timings = []
        self.local_ready_tick = None
        self.verify()

    def verify(self):
        if io.digest(io.read(self.helper)) != self.config["helper_sha256"]:
            raise ValueError("The save helper changed; use Pandemonium Start / repair before writing saves")
        if self.config.get('catalog_protocol') == 2:
            current = io.compatibility(self.data)
            if not hasattr(self, '_save_catalog_cache') or self._save_catalog_cache[0] != current:
                self._save_catalog_cache = (current, io.save_catalog(self.data, self.helper)[0])
            if self._save_catalog_cache[1] != self.config.get('catalog_schema'):
                raise ValueError("This mod's saved definitions changed; Pandemonium will check update compatibility. Your player and Vault are preserved")
        elif catalog(self.data) != self.config["catalog"]:
            raise ValueError("The legacy item catalog changed; use the updated Pandemonium Start / repair to migrate compatibility")
        if not self.test:
            hud = Path(self.config["game_directory"]) / "d2rhud.dll"
            binaries = [(Path(self.config["launcher_path"]), self.config["launcher_sha256"])]
            # The exported interface, rather than unrelated DLL bytes, defines
            # HUD compatibility. Exact pins remain for legacy HUDs and helpers.
            # This declaration never grants save access: native per-page leases,
            # process/session identity and the journal still fence every write.
            if io.hud_contract(hud) is None:
                binaries.append((hud, self.config["hud_sha256"]))
            for path, expected in binaries:
                if io.digest(io.read(path, 256 * 1024 * 1024)).lower() != expected.lower():
                    raise ValueError("The guarded launcher or legacy HUD changed; revalidate custody with Start / repair before writing saves")

    def live_window(self, mode=None, pages=None):
        window = io.document(self.root / 'live-window.json')
        legacy = window.get('schema') == 1 and window.get('phase') == 'parked'
        per_page = window.get('schema') == 2 and window.get('phase') == 'unloaded'
        if (not (legacy or per_page) or
                window.get('owner') != self.config['owner'] or window.get('catalog') != self.config['catalog'] or
                not isinstance(window.get('session'), str) or not re.fullmatch('[A-Za-z0-9-]{1,128}', window['session']) or
                type(window.get('hardcore')) is not bool or type(window.get('pid')) is not int or
                type(window.get('started')) is not int or type(window.get('tick')) is not int or
                not window.get('player') or not 0 <= ticks() - window['tick'] <= 5000):
            raise BlockingIOError('Live stash handoff is unavailable or expired')
        image, started = live_process(window['pid'])
        if (started != window['started'] or game_processes() != {window['pid']} or
                image.parent != Path(self.config['game_directory']).resolve() or
                image.name.lower() not in {'d2r.exe', 'd2r_debug.exe', 'd2r_debug_noerrors.exe'} or
                Path(window.get('save_directory', '')).resolve() != self.folder):
            raise BlockingIOError('Live stash handoff belongs to another game session')
        active_mode = 'HC' if window['hardcore'] else 'SC'
        if mode is not None and mode != active_mode:
            raise BlockingIOError('Live stash handoff belongs to another character mode')
        settings = self.settings(active_mode)
        allowed = {settings['page'], settings['withdraw_page']}
        if window.get('deposit') != settings['page'] or window.get('delivery') != settings['withdraw_page']:
            raise BlockingIOError('The stash is using older market page settings. Close and reopen the stash; if this persists, leave the game and use Pandemonium Start / repair')
        if (type(window.get('loaded')) is not int or not 1 <= window['loaded'] <= 64 or
                legacy and window['loaded'] in allowed or
                type(window.get('parking')) is not int or not 1 <= window['parking'] <= 64 or window['parking'] in allowed or
                not set(allowed if pages is None else pages) <= allowed or
                window['loaded'] in (allowed if pages is None else pages)):
            raise BlockingIOError('The trade pages are not safely unloaded')
        return window

    @contextmanager
    def save_access(self, mode=None, pages=None):
        """Grant only closed-game access or an explicit, unloaded live window.

        Native code must reacquire the shared live.lock before loading a trade
        page. No network/helper work holds either short save-write gate.
        """
        with ExitStack() as stack:
            closed = False
            try:
                stack.enter_context(lease(self.game_lease))
                closed = self.test or not game_running()
            except OSError:
                pass
            window = None
            if not closed:
                stack.close()
                try:
                    stack.enter_context(lease(self.root / 'live.lock'))
                    window = self.live_window(mode, pages)
                except (OSError, ValueError, KeyError) as error:
                    raise BlockingIOError('Marketplace waits for the stash trade pages to unload: ' + str(error)) from None
            yield window

    def save_sidecar(self, path, value, expected):
        mode = 'HC' if '_HC.' in path.name else 'SC'
        with self.save_access(mode, []):
            current = io.document(path) if path.exists() else {}
            if current != expected:
                raise BlockingIOError("Marketplace settings changed; retry after the stash closes")
            io.save(path, value)

    def request_refresh(self, mode, page, reason):
        """Ask the native client to save/park a viewed page; never relax its lease."""
        # Only actual queued escrow deliveries may request a visible reload.
        prefix, separator, identities = reason.partition(':')
        if prefix not in {'items', 'withdraw'} or not separator or not identities or any(not re.fullmatch('[0-9a-f]{32}', identity) for identity in identities.split(',')):
            return False
        try:
            with self.save_access(mode, []) as window:
                if not window or window.get('refresh_supported') != 2 or window.get('loaded') != page or window.get('refresh_id') or window.get('delivery') != page:
                    return False
                key = f"{window['started']}:{page}:{reason}"
                trigger = reason
                seen_path = self.root / 'refresh-seen.json'
                seen = io.document(seen_path) if seen_path.exists() else {}
                previous = seen.get(key, {})
                if previous.get('trigger') == trigger:
                    return False
                path = self.root / 'live-refresh-request.json'
                if path.exists():
                    old = io.document(path)
                    if old.get('kind') == 'gold_deposit' and all(old.get(k) == window[k] for k in ('owner', 'catalog', 'pid', 'started', 'player', 'hardcore', 'session')) and old.get('page') == page and type(old.get('tick')) is int and 0 <= ticks() - old['tick'] < 15000:
                        self.gold_deposit_proof(old.get('gold_deposit'))
                        return True
                    if old.get('session') == window['session'] and old.get('page') == page and old.get('trigger') == trigger and 0 <= ticks() - old.get('tick', 0) < 12000:
                        return True
                request = {k: window[k] for k in ('owner', 'catalog', 'pid', 'started', 'player', 'hardcore', 'session')}
                request.update(schema=1, kind="delivery", id=uuid.uuid4().hex, page=page, tick=ticks(), key=key, trigger=trigger)
                io.save(path, request)
                return True
        except (OSError, ValueError, KeyError):
            return False

    def refresh_ready(self, window):
        if not window or not re.fullmatch('[0-9a-f]{32}', str(window.get('refresh_id', ''))):
            return
        identity = window['refresh_id']
        result = dict(schema=1, id=identity, session=window['session'], ready=True)
        path = self.root / 'live-refresh-result.json'
        if path.exists() and io.document(path) == result:
            return
        io.save(path, result)
        self.local_ready_tick = ticks()
        request_path = self.root / 'live-refresh-request.json'
        if request_path.exists():
            request = io.document(request_path)
            if request.get('id') == identity:
                seen_path = self.root / 'refresh-seen.json'
                seen = io.document(seen_path) if seen_path.exists() else {}
                # Bound this housekeeping file across game sessions.
                seen = dict(list(seen.items())[-63:])
                seen[request['key']] = dict(trigger=request['trigger'], at=time.time())
                io.save(seen_path, seen)

    def live_result(self, window, error=None):
        if window:
            self.refresh_ready(window)
            io.save(self.root / 'live-result.json', dict(schema=1, session=window['session'], at=int(time.time()),
                    terminal=True, pending=self.pending.exists(), error=str(error)[:512] if error else ''))

    def request(self, mode, data):
        began = time.monotonic()
        try:
            return self._request(mode, data)
        finally:
            self.timings.append(dict(operation=data.get('type', ''), milliseconds=round((time.monotonic() - began) * 1000, 1)))

    def _request(self, mode, data):
        data = dict(data, catalog=self.config["catalog"])
        if self.transport:
            return self.transport(mode, data)
        endpoint = self.config["endpoint"]
        url = urllib.parse.urlsplit(endpoint)
        if url.scheme != "https" or url.username or url.password or url.query or url.fragment:
            raise ValueError("Use a plain HTTPS marketplace endpoint")
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, *args, **kwargs):
                return None
        pairing = io.document(self.config["agent_pairing"])
        if pairing["catalog"] != self.config["catalog"] or pairing["owner"] != self.config["owner"] or pairing.get("mod", "RMD-MP") != self.mod or pairing.get("game_mode", "2.4") != "2.4":
            raise ValueError("Agent pairing belongs to another player or catalog")
        req = urllib.request.Request(endpoint + "?" + urllib.parse.urlencode(dict(action="custody", hardcore=int(mode == "HC"), mod=self.mod, version="2.4")),
            json.dumps(data, ensure_ascii=False).encode(), {"Authorization": "Bearer " + pairing["token"], "Content-Type": "application/json"})
        try:
            with urllib.request.build_opener(NoRedirect).open(req, timeout=30) as response:
                raw = response.read(16 * 1024 * 1024 + 1)
            if len(raw) > 16 * 1024 * 1024:
                raise ValueError("Custody response exceeds the size limit")
            return json.loads(raw)
        except urllib.error.HTTPError as error:
            try:
                message = json.loads(error.read(4096)).get("error", "Custody request failed")
            except (ValueError, TypeError):
                message = "Custody request failed"
            raise ValueError(f"Marketplace {error.code}: {message}") from None

    def publish_browser_link(self):
        """Write only a short-lived link code, never player/agent credentials.

        This uses its own sidecar lease and can run while the game holds the
        save lease. Consumed codes refresh on the next companion tick.
        """
        with lease(self.root / "browser-link.lock"):
            target = self.folder / "Marketplace_Web_Link.json"
            current = {}
            if target.exists():
                try:
                    candidate = json.loads(io.read(target, 4096).decode("utf-8-sig"))
                    if candidate.get("schema") == 1 and candidate.get("player_id") == self.config["owner"] and candidate.get("mod") == self.mod and candidate.get("game_mode") == "2.4":
                        current = candidate
                except (ValueError, OSError):
                    pass
            request = dict(type="browser_link")
            if self.mod != "RMD-MP":
                request["identity_catalog"] = io.run_helper(self.helper, self.data, "identity-catalog", "unused")
            if current.get("code"):
                request["code"] = current["code"]
            response = self.request("SC", request)
            if response.get("valid"):
                return
            if not re.fullmatch(r"[A-HJ-NP-Z2-9]{4}(?:-[A-HJ-NP-Z2-9]{4}){2}", response.get("code", "")):
                raise ValueError("The marketplace returned an invalid link code")
            io.save(target, dict(schema=1, game_mode="2.4", mod=self.mod, endpoint=self.config.get("endpoint", "https://market.d2rmodding.com/index.php"),
                                 player_id=self.config["owner"], player_name=response["player_name"], code=response["code"], expires_at=response["expires_at"]))

    def publish_characters(self):
        """Read-only saved character previews, independent of trade save leases.

        Only carried/equipped/mercenary items leave this PC. Parse immutable
        copies, check native checksum, and keep a failed parse from retiring the
        last valid preview. No .d2s bytes are ever replaced or uploaded.
        """
        with lease(self.root / 'profiles.lock'):
            if not self.request('SC', dict(type='profile_state')).get('linked'):
                return
            state_path = self.root / 'profile-sync.json'
            old = io.document(state_path) if state_path.exists() else {}
            current, errors = {}, {}
            for path in sorted(self.folder.glob('*.d2s'))[:128]:
                if self.refresh_active():
                    return  # Preserve the previous manifest; resume preview work later.
                try:
                    if path.is_symlink() or path.resolve().parent != self.folder:
                        continue
                    raw = io.read(path, 8 * 1024 * 1024)
                    if io.read(path, 8 * 1024 * 1024) != raw:
                        raise ValueError('Character save is being updated')
                    digest = io.digest(raw)
                    prior = old.get(path.name, {})
                    if prior.get('hash') == digest:
                        current[path.name] = prior
                        continue
                    with tempfile.TemporaryDirectory(dir=self.root) as work:
                        copy = Path(work) / 'character.d2s'
                        copy.write_bytes(raw)
                        profile = io.run_helper(self.helper, self.data, 'character', copy)
                    mode = 'HC' if profile['hardcore'] else 'SC'
                    result = self.request(mode, dict(type='profile_sync', character=profile, saved_at=int(path.stat().st_mtime)))
                    current[path.name] = dict(hash=digest, key=result['character_key'], mode=mode)
                except (OSError, ValueError, KeyError) as error:
                    errors[path.name] = str(error)[:512]
                    if path.name in old:
                        current[path.name] = old[path.name]
            if self.refresh_active():
                return
            if not errors:
                for mode in ('SC', 'HC'):
                    self.request(mode, dict(type='profile_manifest', characters=[r['key'] for r in current.values() if r['mode'] == mode]))
            io.save(state_path, current)
            io.save(self.root / 'profile-status.json', dict(at=int(time.time()), characters=len(current), errors=errors))

    def settings(self, mode):
        value = io.document(self.folder / f"Marketplace_2.4_{mode}.json")
        if any(value.get(k) != v for k, v in dict(schema=1, game_mode="2.4", mod=self.mod, hardcore=mode == "HC").items()):
            raise ValueError("Save settings belong to a different mod or mode")
        deposit, delivery = value["page"], value["withdraw_page"]
        if type(deposit) is not int or type(delivery) is not int or not 1 <= deposit <= 64 or not 1 <= delivery <= 64:
            raise ValueError("Choose a market page between 1 and 64")
        return value

    def bootstrap(self):
        """Initialize JSON preferences only; never initialize or edit a save.

        A first handoff needs preferences, and preferences used to wait for that
        handoff. The same per-file lease as native Setup breaks this dependency
        without overwriting player choices or enabling deposits.
        """
        path = self.folder / 'Marketplace_2.4_SC.json'
        with self.guard_access():
            if not path.exists() and not self.pending.exists():
                with lease(str(path) + '.lock'):
                    if not path.exists() and not self.pending.exists():
                        io.save(path, dict(schema=1, game_mode='2.4', mod=self.mod, hardcore=False,
                            enabled=False, page=1, withdraw_page=1, single_market_page=True,
                            auto_trade=False, drafts=[], agreements=[], simulation=dict(enabled=False, page=3, drafts=[])))
            capability = dict(schema=1, owner=self.config['owner'], catalog=self.config['catalog'], gold_deposit_refresh=1)
            target = self.root / 'live-capability.json'
            try: previous = io.document(target)
            except (OSError, ValueError): previous = None
            if previous != capability:
                io.save(target, capability)

    def migrate_market_page(self, mode):
        old = self.settings(mode)
        if old.get('single_market_page') is True and old['withdraw_page'] == old['page']:
            return
        if self.pending.exists():
            raise BlockingIOError('Finish the pending transfer before changing market routes')
        backup = self.root / 'migrations' / f'single-market-page-{mode}.json'
        if not backup.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            io.save(backup, old)
        self.save_sidecar(self.folder / f'Marketplace_2.4_{mode}.json',
                          dict(old, withdraw_page=old['page'], single_market_page=True), old)

    def page(self, mode, number):
        return self.folder / f"Stash_{mode}_Page{number}.d2i"

    def plan(self, mode, settings, page, **edit):
        self.verify()
        path = self.page(mode, page)
        with self.save_access(mode, [page]):
            raw = io.read(path)
            if io.read(path) != raw:
                raise ValueError("Save changed while reading")
        payload = dict(schema=1, game_mode="2.4", mod=self.mod, hardcore=mode == "HC", settings=settings,
                       source_hex=raw.hex(), gold_cap=self.config["gold_cap"], **edit)
        with tempfile.TemporaryDirectory(dir=self.root) as work:
            request = Path(work) / "request.json"
            io.save(request, payload)
            result = io.run_helper(self.helper, self.data, "plan", request)
        return raw, bytes.fromhex(result["after_hex"]), result

    def inventory(self, path):
        self.verify()
        match = re.fullmatch(r'Stash_(SC|HC)_Page([0-9]+)\.d2i', path.name)
        if not match or path.parent.resolve() != self.folder:
            raise ValueError('Inventory must belong to the configured shared stash')
        with self.save_access(match[1], [int(match[2])]):
            raw = io.read(path)
        # Parse an immutable snapshot without holding the short native switch gate.
        with tempfile.TemporaryDirectory(dir=self.root) as work:
            snapshot = Path(work) / path.name
            snapshot.write_bytes(raw)
            return io.run_helper(self.helper, self.data, "inventory", snapshot)

    def start(self, mode, request, images=None, extra=None):
        if self.pending.exists():
            raise ValueError("Recover pending custody first")
        if len(json.dumps(dict(request, catalog=self.config["catalog"]), ensure_ascii=False).encode()) > 8 * 1024 * 1024:
            raise ValueError("Custody request exceeds the service limit; no save write was prepared")
        identity = request["id"]
        directory = self.root / "transactions" / identity
        pages = [image[0] for image in images or []]
        if request['type'] == 'claim_prepare':
            pages = [self.settings(mode)['withdraw_page']]
        with self.save_access(mode, pages):
            directory.mkdir()
            record = dict(schema=1, id=identity, mode=mode, request=request, phase="intent", catalog=self.config["catalog"],
                          helper_sha256=self.config["helper_sha256"], at=time.time_ns(), extra=extra or {}, images=[])
            for page, before, after in images or []:
                io.atomic(directory / f"{page}.before", before)
                io.atomic(directory / f"{page}.after", after)
                record["images"].append(dict(page=page, before=io.digest(before), after=io.digest(after)))
            self.persist(record)
            io.save(self.pending, dict(id=identity, mode=mode, pages=pages, reload_safe=False))
            self.fault("intent")
        return self.recover()

    def persist(self, record):
        record.pop("_sha256", None)
        record["_sha256"] = io.digest(json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode())
        io.save(self.root / "transactions" / record["id"] / "record.json", record)

    def recover(self):
        if not self.pending.exists():
            return None
        self.verify()
        pointer = io.document(self.pending)
        identity, mode = pointer["id"], pointer["mode"]
        if not isinstance(identity, str) or len(identity) != 32 or any(c not in "0123456789abcdef" for c in identity) or mode not in {"SC", "HC"}:
            raise ValueError("Invalid custody recovery pointer")
        directory = self.root / "transactions" / identity
        record = io.document(directory / "record.json")
        checksum = record.pop("_sha256", None)
        if checksum != io.digest(json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()):
            raise ValueError("Custody journal checksum failed; trading is paused for reconciliation")
        if record["catalog"] != self.config["catalog"] or record["helper_sha256"] != self.config["helper_sha256"]:
            raise ValueError("Pending custody requires its original verified helper and catalog")
        if record["phase"] == "complete":
            self.finish(record)
            return record
        if record["phase"] == "applied":
            # The local write and its receipt are durable. Native play may have
            # legitimately changed the page since then. Finish only the remote
            # acknowledgement; never re-apply an already delivered image.
            if not io.document(self.pending).get('reload_safe', False):
                io.save(self.pending, dict(id=identity, mode=mode,
                    pages=[image["page"] for image in record["images"]], reload_safe=True))
            self.request(mode, dict(id=identity, type="commit", pages=record["images"]))
            self.fault("committed_remote")
            record["phase"] = "complete"
            self.persist(record)
            self.finish(record)
            return record
        try:
            ticket = self.request(mode, record["request"])
        except ValueError as error:
            if record["request"]["type"] == "claim_prepare" and not record["images"] and any(text in str(error) for text in ("409: No unlisted escrow assets", "409: No unlisted Vault assets")):
                record["phase"] = "empty"
                self.persist(record)
                self.pending.unlink()
                return None
            if record['request']['type'] == 'claim_prepare' and record['phase'] == 'intent' and not record['images'] and str(error).startswith(('Marketplace 409:', 'Marketplace 422:')):
                try:
                    self.request(mode, dict(id=identity, type='status'))
                except ValueError as status_error:
                    if not str(status_error).startswith('Marketplace 404:'):
                        raise
                    record['phase'] = 'rejected'
                    record['error'] = str(error)
                    self.persist(record)
                    self.pending.unlink()
                    if 'No withdrawal resources fit' in str(error):
                        raise CapacityError(str(error), shrink=False) from None
                    return None
            if record['request']['type'] == 'deposit_prepare' and record['phase'] == 'intent' and str(error).startswith(('Marketplace 409:', 'Marketplace 422:')):
                # A rejected deposit must not leave an unnecessary pending journal.
                # Confirm no server ticket exists and every native page is
                # still its original image. Network uncertainty stays pending.
                try:
                    self.request(mode, dict(id=identity, type='status'))
                except ValueError as status_error:
                    if not str(status_error).startswith('Marketplace 404:'):
                        raise
                    if any(io.digest(io.read(self.page(mode, image['page']))) != image['before'] for image in record['images']):
                        raise ValueError('Rejected deposit save image changed; trading needs reconciliation') from error
                    record['phase'] = 'rejected'
                    record['error'] = str(error)
                    self.persist(record)
                    self.pending.unlink()
                    raise DepositRejected(str(error)) from None
            raise
        self.fault("prepared_remote")
        if ticket["status"] == "cancelled":
            record["phase"] = "cancelled"
            self.persist(record)
            self.pending.unlink()
            return record
        if ticket["kind"] == "claim" and not record["images"]:
            try:
                settings = self.settings(mode)
                page = settings["withdraw_page"]
                before, after, result = self.plan(mode, settings, page,
                    credit_items=ticket["packet"]["items"], credit_balances=ticket["packet"]["balances"], currency_bag_id=identity)
                io.atomic(directory / f"{page}.before", before)
                io.atomic(directory / f"{page}.after", after)
                record["images"] = [dict(page=page, before=io.digest(before), after=io.digest(after))]
                record["delivered"] = [dict(item=item["item"], item_hex=placed["item_hex"])
                    for item, placed in zip(ticket["packet"]["items"], result["delivered"], strict=True)]
                record['currency_bags'] = result.get('currency_bags', [])
                record['retained_bags'] = [r['item_hex'] for r in result.get('currency_bags', []) + result['delivered'] if r['code'] == 'Z01' and r['quality'] == 2]
                record['received_balances'] = ticket['packet']['balances']
                self.persist(record)
            except BlockingIOError:
                # The player switched into this page during the cloud request.
                # Keep the journal/reservation and resume when it unloads again.
                raise
            except (OSError, ValueError) as error:
                # No write was authorized. Release the reservation on capacity failure.
                self.request(mode, dict(id=identity, type="cancel"))
                record["phase"] = "cancelled"
                self.persist(record)
                self.pending.unlink()
                raise CapacityError(str(error)) from error
        for image in record["images"]:
            for suffix in ("before", "after"):
                if io.digest(io.read(directory / f"{image['page']}.{suffix}")) != image[suffix]:
                    raise ValueError("Custody recovery image is damaged")
            current = io.digest(io.read(self.page(mode, image["page"])))
            if current not in {image["before"], image["after"]}:
                raise ValueError("Save differs from both authorized images; custody needs reconciliation")
        authorization = dict(id=identity, type="write_authorize", pages=record["images"])
        if ticket["kind"] == "claim":
            authorization["delivered"] = record["delivered"]
        self.request(mode, authorization)
        self.fault("authorized_remote")
        record["phase"] = "applying"
        self.persist(record)
        with self.save_access(mode, [image["page"] for image in record["images"]]) as applied_window:
            # Recheck after authorization: the game may have started (or changed
            # a save) while the cloud request was in flight. Never overwrite it.
            for image in record["images"]:
                if io.digest(io.read(self.page(mode, image["page"]))) not in {image["before"], image["after"]}:
                    raise ValueError("Save changed during authorization; trading needs reconciliation")
            for image in record["images"]:
                path = self.page(mode, image["page"])
                if io.digest(io.read(path)) == image["before"]:
                    io.atomic(path, io.read(directory / f"{image['page']}.after"))
                if io.digest(io.read(path)) != image["after"]:
                    raise ValueError("Save write verification failed; trading is paused for reconciliation")
                self.fault("page_written")
            # Publish this before releasing the short write gate. A slow/lost
            # cloud commit must not keep a completely written stash unloaded.
            record["phase"] = "applied"
            self.persist(record)
            io.save(self.pending, dict(id=identity, mode=mode,
                pages=[image["page"] for image in record["images"]], reload_safe=True))
            if ticket['kind'] == 'claim':
                self.refresh_ready(applied_window)
            self.fault("locally_applied")
        self.request(mode, dict(id=identity, type="commit", pages=record["images"]))
        self.fault("committed_remote")
        record["phase"] = "complete"
        self.persist(record)
        self.finish(record)
        return record

    def finish(self, record):
        # Re-applying finish is harmless, including after a lost remote acknowledgement.
        state_path = self.root / "lineage.json"
        state = io.document(state_path) if state_path.exists() else dict(delivered={}, consumed={}, receipts=[])
        if record["id"] not in state["receipts"]:
            old = self.settings(record['mode'])
            identities = {positionless(r['item_hex']) for r in record['request'].get('items', [])}
            identities.update(positionless(r['item_hex']) for r in record.get('delivered', []))
            kept = [r for r in old['drafts'] if positionless(r['item_hex']) not in identities]
            if kept != old['drafts']:
                self.save_sidecar(self.folder / f"Marketplace_2.4_{record['mode']}.json", dict(old, drafts=kept), old)
            for n, item in enumerate(record.get('currency_bags', [])):
                state.setdefault('currency_bags', {})[record['id'] + ':' + str(n)] = dict(mode=record['mode'], item_hex=item['item_hex'])
            if record.get('retained_bags'):
                previous = state.setdefault('retained_bags', {}).get(record['mode'], {})
                retained = previous.get('items', []) if previous.get('page') == record['images'][0]['page'] else []
                state['retained_bags'][record['mode']] = dict(page=record['images'][0]['page'], items=list(dict.fromkeys(retained + record['retained_bags'])))
            gold = sum(r['quantity'] for r in record.get('received_balances', []) if r['kind'] == 'gold')
            if gold:
                route = record['images'][0]['page']
                previous = state.setdefault('retained_gold', {}).get(record['mode'], {})
                quantity = previous.get('quantity', 0) if previous.get('page') == route else 0
                state['retained_gold'][record['mode']] = dict(page=route, quantity=quantity + gold)
            for image in record["images"]:
                state["consumed"][record["mode"] + ":" + str(image["page"]) + ":" + image["before"]] = record["id"]
            for item in record.get("delivered", []):
                state["delivered"][item["item"]] = dict(mode=record["mode"], item_hex=item["item_hex"])
            for item in record["request"].get("items", []):
                state["delivered"].pop(item.get("ledger_id"), None)
            state["receipts"].append(record["id"])
            io.save(state_path, state)
        markers = [v['item_hex'] for field in ('delivered', 'currency_bags') for v in state.get(field, {}).values() if v.get('mode') == record['mode']]
        path = self.folder / f"MarketplaceReceipts_2.4_{record['mode']}.json"
        value = dict(schema=1, mod=self.mod, hardcore=record['mode'] == 'HC', items=markers[-4096:])
        # This value-only receipt sidecar is owned by the agent, not a save.
        # Applied recovery must remain possible even while the native lease is held.
        if not path.exists() or io.document(path) != value:
            io.save(path, value)
        self.pending.unlink(missing_ok=True)

    @staticmethod
    def web_item(row, page):
        item = {key: row[key] for key in ("code", "saved_id", "quality", "index", "quantity", "quantity_bit", "tab", "item_hex")}
        item["item_uuid"] = row.get("item_uuid", "")
        item.update(page=page, ethereal=int(row["ethereal"]), identified=int(row["identified"]), sockets=len(row["socketed_items"]))
        item["display"] = {key: row[key] for key in ("level", "picture", "flags", "saved_properties", "socketed_items")}
        return item

    def delivery_queue(self, mode, response=None):
        # Fetch returns/withdrawals without publishing a stale or empty snapshot
        # of the deposit page while it is loaded by the game.
        if response is None:
            response = self.request(mode, dict(id=uuid.uuid4().hex, type='delivery_queue'))
        deliveries = response.get('delivery_items', [])
        if not isinstance(deliveries, list) or len(deliveries) > 64 or any(not isinstance(i, str) or not re.fullmatch('[0-9a-f]{32}', i) for i in deliveries):
            raise ValueError('Invalid requested delivery list')
        withdrawals = response.get('wallet_withdrawals', [])
        if not isinstance(withdrawals, list) or len(withdrawals) > 64 or any(not isinstance(w, dict) or not isinstance(w.get('id'), str) or not re.fullmatch('[0-9a-f]{32}', w['id']) for w in withdrawals):
            raise ValueError('Invalid Vault withdrawal queue')
        self.delivery_items[mode] = deliveries
        self.wallet_withdrawals[mode] = withdrawals
        funding = response.get('funding_requests', [])
        if not isinstance(funding, list) or len(funding) > 64 or any(not isinstance(r, dict) or not re.fullmatch('[0-9a-f]{32}', str(r.get('id', ''))) or not isinstance(r.get('price'), list) for r in funding):
            raise ValueError('Invalid agreed trade funding queue')
        self.funding_requests[mode] = funding

    def clear_market_sidecars(self, mode):
        """Clear trade-only drafts after a server reset; never edit game saves."""
        if self.pending.exists():
            raise BlockingIOError('Recover the pending save operation before clearing old trade drafts')
        for stem, fields in [('MarketplaceActivityLocal', ('threads', 'commands')),
                             ('MarketplaceChatOutbox', ('commands',))]:
            path = self.folder / f'{stem}_2.4_{mode}.json'
            if not path.exists():
                continue
            old = io.document(path)
            if any(old.get(k) != v for k, v in dict(schema=1, game_mode='2.4', mod=self.mod, hardcore=mode == 'HC').items()):
                raise ValueError('Old trade sidecar belongs to another mod or game mode')
            value = dict(old)
            for field in fields:
                value[field] = []
            self.save_sidecar(path, value, old)
        path = self.root / 'lineage.json'
        if path.exists():
            with self.save_access(mode, []):
                old = io.document(path)
                value = dict(old)
                for field in ('delivered', 'consumed'):
                    value[field] = {k: v for k, v in old.get(field, {}).items() if v.get('mode') != mode}
                io.save(path, value)

    def web_sync(self, mode):
        """Apply website intents under closed-game or parked-page save access.

        A repeated intent produces the same route/asking draft after a crash.
        The server acknowledges it only after the sidecar is durably replaced.
        This never bypasses the journal used to move actual items.
        """
        acknowledgements = []
        for _ in range(3):
            settings = self.settings(mode)
            original_settings = json.loads(json.dumps(settings))
            try:
                inventory = self.inventory(self.page(mode, settings["page"]))
            except BlockingIOError:
                raise
            except (OSError, ValueError):
                # A new/unfinished stash page must not prevent launching the
                # game to create or repair it. No item is published from it.
                inventory = {"items": []}
            repaired = False
            for draft in settings['drafts']:
                if any(r['tab'] == draft['tab'] and r['item_hex'] == draft['item_hex'] for r in inventory['items']):
                    continue
                matches = [r for r in inventory['items'] if r['tab'] == draft['tab'] and
                           r.get('legacy_item_hex') and positionless(r['legacy_item_hex']) == positionless(draft['item_hex'])]
                if len(matches) == 1:
                    draft['item_hex'] = matches[0]['item_hex']
                    repaired = True
            if repaired:
                self.save_sidecar(self.folder / f'Marketplace_2.4_{mode}.json', settings, original_settings)
            # Publish canonical saved bytes once the game is closed. A live
            # client snapshot with omitted seed cannot establish item identity.
            publication_path = self.folder / f'MarketplacePublish_2.4_{mode}.json'
            priced = []
            if settings['enabled']:
                for row in inventory['items']:
                    draft = next((d for d in settings['drafts'] if d['tab'] == row['tab'] and d['item_hex'] == row['item_hex']), None)
                    if draft and draft.get('price'):
                        priced.append(dict(row, price=draft['price'], **({'comment': draft['comment']} if 'comment' in draft else {})))
            packet = dict(schema=1, game_mode='2.4', mod=self.mod, hardcore=mode == 'HC',
                          page=settings['page'], enabled=settings['enabled'], source='saved', custody='local_save', purchasable=False, items=priced)
            previous = io.document(publication_path) if publication_path.exists() else {}
            comparison = {k:v for k,v in previous.items() if k not in {'id','captured_at'}}
            if packet != comparison or previous.get('captured_at', 0) < time.time() - 60:
                self.save_sidecar(publication_path, dict(packet, id=uuid.uuid4().hex, captured_at=int(time.time())), previous)
            rows = [self.web_item(row, settings["page"]) for row in inventory["items"]]
            history_path = self.root / "lineage.json"
            delivered = io.document(history_path).get("delivered", {}) if history_path.exists() else {}
            for row in rows:
                matches = [key for key, value in delivered.items() if value["mode"] == mode and positionless(value["item_hex"]) == positionless(row["item_hex"])]
                if len(matches) == 1:
                    row["ledger_id"] = matches[0]
                draft = next((d for d in settings["drafts"] if d["tab"] == row["tab"] and d["item_hex"] == row["item_hex"]), None)
                if draft:
                    row["price"] = draft["price"]
                    if "comment" in draft:
                        row["comment"] = draft["comment"]
            response = self.request(mode, dict(type="web_sync", features=web_features(self.config), companion_version=companion_version(), settings={key: settings[key] for key in ("page", "withdraw_page", "enabled", "auto_trade")}, inventory=rows, acknowledged=acknowledgements))
            self.delivery_queue(mode, response)
            acknowledgements = []
            if not response["intents"]:
                return
            for intent in response["intents"]:
                packet = intent["packet"]
                try:
                    current = self.settings(mode)
                    original_current = json.loads(json.dumps(current))
                    if intent["kind"] == "setup":
                        if current["page"] != packet["page"]:
                            current["drafts"] = []
                        current.update(packet)
                        current.update(withdraw_page=current['page'], single_market_page=True)
                    elif intent["kind"] == "price":
                        if current["page"] != packet["page"] or not any(row["tab"] == packet["tab"] and row["item_hex"] == packet["item_hex"] for row in self.inventory(self.page(mode, current["page"]))["items"]):
                            raise ValueError("The stash item moved or changed before its asking price could be applied")
                        previous_draft = next((row for row in current["drafts"] if row["tab"] == packet["tab"] and row["item_hex"] == packet["item_hex"]), {})
                        current["drafts"] = [row for row in current["drafts"] if not (row["tab"] == packet["tab"] and row["item_hex"] == packet["item_hex"])]
                        if packet["price"]:
                            current["drafts"].append({**{key: packet[key] for key in ("tab", "item_hex", "price")}, **({"comment": packet.get("comment", previous_draft.get("comment", ""))} if "comment" in packet or "comment" in previous_draft else {})})
                    elif intent["kind"] == "reset":
                        self.clear_market_sidecars(mode)
                        current['enabled'] = current['auto_trade'] = False
                        current['drafts'] = []
                        current['agreements'] = []
                        if isinstance(current.get('simulation'), dict):
                            current['simulation']['enabled'] = False
                            current['simulation']['drafts'] = []
                    else:
                        raise ValueError("Unsupported website request")
                    # Reuse route validation before committing the settings file.
                    if type(current["page"]) is not int or type(current["withdraw_page"]) is not int or not 1 <= current["page"] <= 64 or not 1 <= current["withdraw_page"] <= 64:
                        raise ValueError("Choose a valid market page")
                    self.save_sidecar(self.folder / f"Marketplace_2.4_{mode}.json", current, original_current)
                    if intent['kind'] == 'reset':
                        marker = dict(schema=1, game_mode='2.4', mod=self.mod, hardcore=mode == 'HC', id=intent['id'], generation=packet['generation'])
                        self.save_sidecar(self.folder / f'MarketplaceReset_2.4_{mode}.json', marker,
                                          io.document(self.folder / f'MarketplaceReset_2.4_{mode}.json') if (self.folder / f'MarketplaceReset_2.4_{mode}.json').exists() else {})
                    acknowledgements.append(dict(id=intent["id"], status="applied", message=""))
                except (ValueError, KeyError) as error:
                    acknowledgements.append(dict(id=intent["id"], status="failed", message=str(error)[:512]))
        if acknowledgements:
            # Acknowledge the last batch without recursively applying new work.
            settings = self.settings(mode)
            try:
                rows = [self.web_item(row, settings["page"]) for row in self.inventory(self.page(mode, settings["page"]))["items"]]
            except BlockingIOError:
                raise
            except (OSError, ValueError):
                rows = []
            self.request(mode, dict(type="web_sync", features=web_features(self.config), companion_version=companion_version(), settings={key: settings[key] for key in ("page", "withdraw_page", "enabled", "auto_trade")}, inventory=rows, acknowledged=acknowledgements))

    @staticmethod
    def gold_deposit_proof(value):
        keys = ('carried_before', 'carried_after', 'stash_before', 'stash_after')
        if not isinstance(value, dict) or any(type(value.get(k)) is not int or value[k] < 0 for k in keys):
            raise ValueError('Invalid manual gold deposit proof')
        cb, ca, sb, sa = (value[k] for k in keys)
        if cb > 2147483647 or ca >= cb or sa <= sb or sa > 7 * 2147483647 or cb - ca != sa - sb:
            raise ValueError('Carried gold and market gold do not match the deposit')
        return {k: value[k] for k in keys}

    def deposit(self, mode, *, gold_only=False, release_retained_gold=False):
        settings = self.settings(mode)
        if not settings["enabled"]:
            return False
        page = settings["page"]
        path = self.page(mode, page)
        with self.save_access(mode, [page]):
            before = io.read(path)
        state = io.document(self.root / "lineage.json") if (self.root / "lineage.json").exists() else dict(delivered={}, consumed={})
        # A page hash is a write precondition, not an item identity. Depositing
        # the same amount of gold twice can reproduce exactly the same bytes.
        # Keep consumed images for audit/recovery; admission of unique items is
        # enforced by their saved identity and server ownership, not this hash.
        inventory = self.inventory(path)
        items, selectors, resources = [], [], {}
        previous = state.setdefault('retained_bags', {}).get(mode, {})
        present = {positionless(r['item_hex']) for r in inventory['items'] if r['code'] == 'Z01'}
        retained_bags = [h for h in previous.get('items', []) if positionless(h) in present] if previous.get('page') == page else []
        if previous.get('items', []) != retained_bags:
            state['retained_bags'][mode] = dict(page=page, items=retained_bags)
            with self.save_access(mode, [page]):
                io.save(self.root / 'lineage.json', state)
        retained_keys = {positionless(h) for h in retained_bags}
        for row in ([] if gold_only else inventory["items"]):
            draft = next((d for d in settings["drafts"] if d["tab"] == row["tab"] and d["item_hex"] == row["item_hex"]), None)
            price = draft.get("price") if draft else None
            if len(items) < 64 and price:
                item = self.web_item(row, page)
                if price:
                    item["price"] = price
                    if "comment" in draft:
                        item["comment"] = draft["comment"]
                lineage = [key for key, value in state["delivered"].items() if value["mode"] == mode and positionless(value["item_hex"]) == positionless(row["item_hex"])]
                if len(lineage) > 1:
                    raise ValueError("Ambiguous item lineage; do not merge identical seeds or bytes")
                if lineage:
                    item["ledger_id"] = lineage[0]
                items.append(item)
                selectors.append({k: row[k] for k in ("tab", "ordinal", "item_hex")})
            elif not price and row["code"] == "Z01" and row["quality"] == 2:
                # A currency parcel just delivered here stays available to take or cube.
                if positionless(row['item_hex']) in retained_keys:
                    continue
                for balance in row["balances"]:
                    resources[balance["stat"]] = min(2000000000, resources.get(balance["stat"], 0) + balance["quantity"])
        balances = [dict(kind="bag", code="Z01", label=stat, stat=stat, quality=2, index=-1, ethereal=-1, quantity=q) for stat, q in sorted(resources.items()) if q][:15]
        retained = state.setdefault('retained_gold', {}).get(mode, {})
        floor = min(inventory['gold'], retained.get('quantity', 0)) if retained.get('page') == page and not release_retained_gold else 0
        if retained.get('quantity', 0) != floor:
            state['retained_gold'][mode] = dict(page=page, quantity=floor)
            with self.save_access(mode, [page]):
                io.save(self.root / 'lineage.json', state)
        if inventory['gold'] > floor:
            balances.append(dict(kind="gold", code="gld", label="Gold", quality=2, index=-1, ethereal=-1, quantity=min(inventory['gold'] - floor, 2000000000)))
        if not items and not balances:
            return False
        excluded = [dict(tab=r['tab'], item_hex=r['item_hex']) for r in inventory['items'] if positionless(r['item_hex']) in retained_keys and
                    not any(d['tab'] == r['tab'] and d['item_hex'] == r['item_hex'] for d in settings['drafts'])]
        raw, after, result = self.plan(mode, settings, page, take=selectors, debit=balances, debit_exclude=excluded)
        if raw != before:
            raise ValueError("Deposit page changed before planning")
        request = dict(id=uuid.uuid4().hex, type="deposit_prepare", items=items, balances=balances,
                       pages=[dict(page=page, before=io.digest(before), after=io.digest(after))])
        self.start(mode, request, [(page, before, after)])
        return True

    def fund_agreed_trade(self, mode):
        settings = self.settings(mode)
        if not settings['enabled']:
            return False
        for offer in self.funding_requests.get(mode, []):
            try:
                before, after, result = self.plan(mode, settings, settings['page'], debit=offer['price'])
            except ValueError:
                continue  # No complete local payment: leave every local item untouched.
            if before == after:
                continue
            snapshot = self.inventory(self.page(mode, settings['page']))
            items = []
            for row in result['removed']['items']:
                original = next(r for r in snapshot['items'] if r['tab'] == row['tab'] and r['ordinal'] == row['ordinal'])
                items.append(self.web_item(dict(row, quantity_bit=original['quantity_bit']), settings['page']))
            state = io.document(self.root / 'lineage.json') if (self.root / 'lineage.json').exists() else {}
            for row in items:
                matches = [k for k,v in state.get('delivered', {}).items() if v['mode'] == mode and positionless(v['item_hex']) == positionless(row['item_hex'])]
                if len(matches) > 1:
                    raise ValueError('Ambiguous agreed payment lineage')
                if matches:
                    row['ledger_id'] = matches[0]
            request = dict(id=uuid.uuid4().hex, type='deposit_prepare', funding_offer=offer['id'], funding_revision=offer['revision'], items=items, balances=result['removed']['balances'],
                           pages=[dict(page=settings['page'], before=io.digest(before), after=io.digest(after))])
            try:
                self.start(mode, request, [(settings['page'], before, after)])
            except DepositRejected:
                continue  # Rejected/countered while planning: no local item was changed.
            return True
        return False

    def claim(self, mode, item_limit=64, item_ids=None, withdrawal_id=None):
        try:
            settings = self.settings(mode)
            page = self.page(mode, settings["withdraw_page"])
            inventory = self.inventory(page)
        except BlockingIOError:
            raise
        except (OSError, ValueError, KeyError) as error:
            raise CapacityError("Delivery page is not ready: " + str(error), shrink=False) from error
        # Capacity is for a freshly generated bag, never for a player's existing bag.
        capacities = {cap['stat']: cap['quantity'] for cap in inventory.get('bag_parcel_capacity', [])}
        balances = [dict(kind="bag", code="Z01", label=stat, stat=stat, quality=2, index=-1, ethereal=-1, quantity=q)
                    for stat, q in sorted(capacities.items()) if q][:15]
        import struct
        with self.save_access(mode, [settings['withdraw_page']]):
            raw = io.read(page)
        at, room = 0, 0
        for _ in range(7):
            room += max(0, self.config["gold_cap"] - struct.unpack_from("<I", raw, at+12)[0])
            at += struct.unpack_from("<I", raw, at+16)[0]
        if room:
            balances.append(dict(kind="gold", code="gld", label="Gold", quality=2, index=-1, ethereal=-1, quantity=min(room, 2000000000)))
        request = dict(id=uuid.uuid4().hex, type="claim_prepare", item_limit=item_limit, balance_caps=balances)
        if item_ids is not None:
            # Explicit returns must not sweep unrelated payment stock or funds
            # out of the escrow wallet on each background poll.
            request.update(item_ids=item_ids[:item_limit], balance_caps=[])
        if withdrawal_id is not None:
            if not balances:
                raise CapacityError("Make room on your market page for gold or a new currency bag.", shrink=False)
            request.update(item_ids=[], item_limit=0, withdrawal_id=withdrawal_id)
        return self.start(mode, request)

    def collect_wallet_withdrawals(self, mode):
        delivered = 0
        for withdrawal in self.wallet_withdrawals.get(mode, []):
            identity = withdrawal['id']
            try:
                result = self.claim(mode, withdrawal_id=identity)
                if result:
                    delivered += 1
                message = ''
            except CapacityError:
                message = 'Make room on your market page for gold or a new currency bag. The remaining amount stays reserved.'
            self.request(mode, dict(id=uuid.uuid4().hex, type='wallet_notice', withdrawal_id=identity, message=message))
        return delivered

    def collect_deliveries(self, mode, item_ids=None):
        count, limit = 0, 64
        remaining = None if item_ids is None else list(item_ids)
        while count < 32 and (remaining is None or remaining):
            try:
                if not self.claim(mode, limit, remaining):
                    break
                count += 1
                if remaining is not None:
                    remaining = remaining[limit:]
                limit = 64
            except CapacityError as error:
                if error.shrink and limit > 1:
                    limit //= 2
                    continue
                return str(error)
        return count

    def deposit_parked_gold(self, window, warnings, rejected):
        """Process an explicit carried-to-stash deposit before unrelated sync work."""
        began = time.monotonic()
        mode = 'HC' if window['hardcore'] else 'SC'
        identity = window['refresh_id']
        count = 0
        try:
            proof = self.gold_deposit_proof(window.get('gold_deposit'))
            settings = self.settings(mode)
            page = settings['page']
            if not settings['enabled'] or window.get('deposit') != page or window.get('loaded') == page:
                raise BlockingIOError('The market page must finish its native gold handoff')
            state_path = self.root / 'lineage.json'
            with self.save_access(mode, [page]):
                state = io.document(state_path) if state_path.exists() else dict(delivered={}, consumed={})
                requests = state.setdefault('gold_deposit_requests', {})
                accepted = requests.get(identity)
                if accepted and (accepted.get('mode') != mode or accepted.get('page') != page or accepted.get('proof') != proof):
                    raise ValueError('Manual gold deposit identity changed')
            if not accepted:
                inventory = self.inventory(self.page(mode, page))
                if inventory['gold'] != proof['stash_after']:
                    raise ValueError('Market gold changed before the manual deposit was saved; your funds are preserved')
                with self.save_access(mode, [page]):
                    state = io.document(state_path) if state_path.exists() else dict(delivered={}, consumed={})
                    requests = state.setdefault('gold_deposit_requests', {})
                    requests[identity] = dict(mode=mode, page=page, proof=proof, complete=False)
                    if len(requests) > 128:
                        for old in list(requests):
                            if old != identity and requests[old].get('complete') and len(requests) > 128:
                                del requests[old]
                    io.save(state_path, state)
            if not accepted or not accepted.get('complete'):
                while count < 32 and self.deposit(mode, gold_only=True, release_retained_gold=True):
                    count += 1
                with self.save_access(mode, [page]):
                    state = io.document(state_path)
                    state['gold_deposit_requests'][identity]['complete'] = True
                    io.save(state_path, state)
            warnings.pop(mode, None)
        except DepositRejected as error:
            rejected[mode] = str(error)
        except (BlockingIOError, ValueError) as error:
            if self.pending.exists():
                raise
            rejected[mode] = str(error)
        io.save(self.root / 'status.json', dict(at=int(time.time()), pending=self.pending.exists(), result={mode: count}, deliveries={}, delivery_warnings=warnings, rejected_deposits=rejected))
        self.live_result(window)
        io.save(self.root / 'delivery-timing.json', dict(schema=1, kind='gold_deposit', refresh_id=identity,
            at=int(time.time()), milliseconds=round((time.monotonic() - began) * 1000, 1), local_ready_tick=self.local_ready_tick, requests=self.timings))
        return {mode: count}

    def refresh_active(self):
        # Priority hint only. Every read/write still obtains the real save lease.
        try:
            window = self.live_window(pages=[])
            if re.fullmatch('[0-9a-f]{32}', str(window.get('refresh_id', ''))):
                return True
            request = io.document(self.root / 'live-refresh-request.json')
            return (request.get('kind') in {'delivery', 'gold_deposit'} and request.get('session') == window['session']
                    and re.fullmatch('[0-9a-f]{32}', str(request.get('id', ''))) is not None
                    and request.get('owner') == window['owner'] and request.get('started') == window['started']
                    and request.get('pid') == window['pid'] and request.get('catalog') == window['catalog']
                    and request.get('page') == window['delivery' if request.get('kind') == 'delivery' else 'deposit'] == window['loaded']
                    and type(request.get('tick')) is int and 0 <= ticks() - request['tick'] <= 15000)
        except (OSError, ValueError, KeyError):
            return False

    def deliver_parked_page(self, window, warnings, rejected):
        """Only queued deliveries while native UI is waiting for this handoff.

        Defer listing publication, deposits, browser-link renewal and character
        upload. Remote reservations/authorization and durable save receipts are
        unchanged. No unrelated request holds the player on the parking page.
        """
        began = time.monotonic()
        mode = 'HC' if window['hardcore'] else 'SC'
        self.delivery_queue(mode)
        delivered = 0
        try:
            if self.delivery_items.get(mode):
                delivered = self.collect_deliveries(mode, self.delivery_items[mode])
            self.collect_wallet_withdrawals(mode)
            if isinstance(delivered, str):
                warnings[mode] = delivered
            else:
                warnings.pop(mode, None)
        except BlockingIOError:
            warnings[mode] = 'Delivery will refresh automatically when the stash is idle.'
        io.save(self.root / 'status.json', dict(at=int(time.time()), pending=self.pending.exists(), result={mode: 0}, deliveries={mode: delivered}, delivery_warnings=warnings, rejected_deposits=rejected))
        self.live_result(window)
        # One compact diagnostic per refresh, without per-request disk writes.
        io.save(self.root / 'delivery-timing.json', dict(schema=1, refresh_id=window['refresh_id'],
            at=int(time.time()), milliseconds=round((time.monotonic() - began) * 1000, 1),
            local_ready_tick=self.local_ready_tick, requests=self.timings))
        return {mode: 0}

    @contextmanager
    def guard_access(self):
        # Hold the scheduling gate only while acquiring the transfer lock.
        # Repair can then claim it while a current operation finishes, without
        # letting another polling cycle beat it to guard.lock. Actual save
        # writes still require their separate native/closed-game leases.
        with ExitStack() as guard:
            with ExitStack() as gate:
                try:
                    gate.enter_context(lease(self.root / 'repair.lock'))
                except OSError as error:
                    if isinstance(error, BlockingIOError) or getattr(error, 'winerror', None) in (32, 33):
                        raise RepairInProgress('Marketplace repair is in progress; background sync will resume automatically') from None
                    raise
                guard.enter_context(lease(self.root / 'guard.lock'))
            yield

    def run(self, preflight=False):
        with self.guard_access():
            rejected = {}
            try:
                # A durable applied receipt only needs its cloud acknowledgement.
                # Do not make that recovery wait for an unrelated loaded page or
                # native read lease. Pre-write recovery retains its own write gate.
                self.recover()
            except DepositRejected as error:
                rejected['recovered'] = str(error)
            with self.save_access(pages=[]) as window:
                live_mode = ("HC" if window["hardcore"] else "SC") if window else None
            results = {}
            status_path = self.root / "status.json"
            warnings = io.document(status_path).get("delivery_warnings", {}) if status_path.exists() else {}
            deliveries = {}
            if not preflight and window and re.fullmatch('[0-9a-f]{32}', str(window.get('refresh_id', ''))):
                if window.get('refresh_kind') == 'gold_deposit':
                    return self.deposit_parked_gold(window, warnings, rejected)
                return self.deliver_parked_page(window, warnings, rejected)
            if not preflight and window and self.refresh_active():
                # A verified request is waiting for the native parking ACK.
                # Do not queue unrelated HTTP work ahead of that short handoff.
                return {live_mode: 0}
            for mode in ("SC", "HC"):
                if live_mode is not None and mode != live_mode:
                    continue
                if not (self.folder / f"Marketplace_2.4_{mode}.json").exists():
                    continue
                try:
                    self.migrate_market_page(mode)
                    self.web_sync(mode)
                except BlockingIOError:
                    pass  # Deposit is loaded; its private snapshot stays intact.
                self.delivery_queue(mode)
                if preflight:
                    try:
                        results[mode] = self.collect_deliveries(mode, self.delivery_items.get(mode, []))
                    except BlockingIOError:
                        results[mode] = 'Switch away from your delivery page to receive queued trades.'
                else:
                    count = 0
                    try:
                        while count < 32 and self.deposit(mode):
                            count += 1
                        results[mode] = count
                    except DepositRejected as error:
                        results[mode] = str(error)
                        rejected[mode] = str(error)
                    except BlockingIOError:
                        results[mode] = count
                    except (OSError, ValueError) as error:
                        # A rejected/read-only deposit must not starve returns
                        # or wallet withdrawals. Authorized/uncertain work still
                        # freezes the journal and must be recovered first.
                        if self.pending.exists():
                            raise
                        results[mode] = str(error)
                        rejected[mode] = str(error)
                    try:
                        self.web_sync(mode)
                    except BlockingIOError:
                        pass
                    self.delivery_queue(mode)
                    try:
                        if self.fund_agreed_trade(mode):
                            self.delivery_queue(mode)
                    except BlockingIOError:
                        pass
                    if self.delivery_items.get(mode):
                        try:
                            delivered = self.collect_deliveries(mode, self.delivery_items[mode])
                        except BlockingIOError:
                            delivered = 'Delivery will refresh automatically when the stash is idle.'
                            self.request_refresh(mode, self.settings(mode)['withdraw_page'], 'items:' + ','.join(self.delivery_items[mode]))
                        deliveries[mode] = delivered
                        if isinstance(delivered, str):
                            warnings[mode] = delivered
                        else:
                            warnings.pop(mode, None)
                try:
                    self.collect_wallet_withdrawals(mode)
                    if warnings.get(mode) in ('Switch away from your delivery page to receive queued withdrawals.', 'Delivery will refresh automatically when the stash is idle.'):
                        warnings.pop(mode, None)
                except BlockingIOError:
                    warnings[mode] = 'Delivery will refresh automatically when the stash is idle.'
                    ids = ','.join(io.digest((w['id'] + ':' + str(w.get('revision', 0))).encode())[:32] for w in self.wallet_withdrawals.get(mode, []))
                    self.request_refresh(mode, self.settings(mode)['withdraw_page'], 'withdraw:' + ids)
            if preflight:
                for mode, value in results.items():
                    if isinstance(value, str):
                        warnings[mode] = value
                    else:
                        warnings.pop(mode, None)
            io.save(status_path, dict(at=int(time.time()), phase='ready', error='', pending=self.pending.exists(), result=results, deliveries=deliveries, delivery_warnings=warnings, rejected_deposits=rejected))
            self.live_result(window)
            return results


def report_failure(config, error):
    """Surface even constructor/verification failures; never authorize or write saves."""
    root = Path(config['state_directory'])
    status_path = root / 'status.json'
    status = io.document(status_path) if status_path.exists() else {}
    message = str(error)[:512]
    status.update(at=int(time.time()), phase='retrying', error=message, pending=(root / 'pending.json').exists())
    io.save(status_path, status)
    try:
        window = io.document(root / 'live-window.json')
        if (window.get('owner') == config.get('owner') and window.get('catalog') == config.get('catalog')
                and isinstance(window.get('session'), str)):
            identity = window.get('refresh_id', '')
            if re.fullmatch('[0-9a-f]{32}', str(identity)):
                io.save(root / 'live-refresh-result.json', dict(schema=1, id=identity, session=window['session'], ready=True))
            io.save(root / 'live-result.json', dict(schema=1, session=window['session'], at=int(time.time()),
                terminal=True, pending=status['pending'], error=message))
    except (OSError, ValueError):
        pass


def report_waiting(config):
    # A missing/contended native grant is an ordinary startup transition, not a
    # reason to relaunch. Preserve real pending recovery errors and receipts.
    root = Path(config['state_directory'])
    if (root / 'pending.json').exists():
        return
    path = root / 'status.json'
    status = io.document(path) if path.exists() else {}
    status.update(at=int(time.time()), phase='waiting_for_stash', pending=False, error='',
                  message='Connecting to the game. Open the stash once; syncing then continues automatically.')
    io.save(path, status)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("--preflight", action="store_true")
    parser.add_argument("--watch", action="store_true")
    args = parser.parse_args()
    initial = io.document(args.config)
    wake = WakeEvent(initial.get('owner', '')) if args.watch else None
    last_link = 0
    last_profiles = 0
    optional = None
    while True:
        agent = None
        config = {}
        try:
            config = io.document(args.config)
            # Managed upgrades stop between operations, with every receipt durable.
            if args.watch and config.get('managed_runtime') and Path(config['managed_runtime']).resolve() != Path(__file__).resolve().parent and not (Path(config['state_directory']) / 'pending.json').exists():
                break
            agent = Agent(config)
            if args.watch:
                agent.bootstrap()
            delivery_request = agent.root / 'delivery-request.json'
            if args.watch and delivery_request.exists() and not game_running():
                requested = io.read(delivery_request)
                print(json.dumps(agent.run(preflight=True)), flush=True)
                if delivery_request.exists() and io.read(delivery_request) == requested:
                    delivery_request.unlink()
            print(json.dumps(agent.run(args.preflight)), flush=True)
        except RepairInProgress as error:
            if not args.watch:
                raise SystemExit(str(error)) from None
            # Do not replace a real transfer receipt/error with a normal pause.
        except BlockingIOError as error:
            if not args.watch:
                raise SystemExit(str(error)) from None
            if config:
                try: report_waiting(config)
                except (OSError, ValueError, KeyError): pass
        except (OSError, ValueError, KeyError) as error:
            if args.watch and config:
                try:
                    report_failure(config, error)
                except (OSError, ValueError, KeyError):
                    pass
            if not args.watch:
                raise SystemExit(str(error)) from None
            print(str(error), flush=True)
        # Read-only account linking/character uploads do not depend on acquiring
        # the native page lease. A player can link before ever opening the stash.
        if agent is not None and args.watch and not args.preflight and not agent.refresh_active() and (optional is None or not optional.is_alive()):
            now = time.monotonic()
            link_due, profiles_due = now - last_link >= 15, now - last_profiles >= 60
            if link_due or profiles_due:
                if link_due: last_link = now
                if profiles_due: last_profiles = now
                def publish_optional(snapshot=dict(config), link=link_due, profiles=profiles_due):
                    try:
                        publisher = Agent(snapshot)
                        if link and not publisher.refresh_active(): publisher.publish_browser_link()
                        if profiles and not publisher.refresh_active(): publisher.publish_characters()
                    except (OSError, ValueError, KeyError) as error:
                        print('Optional marketplace sync: ' + str(error), flush=True)
                optional = threading.Thread(target=publish_optional, daemon=True)
                optional.start()
        if not args.watch:
            break
        path = Path(initial['state_directory']) / 'live-window.json'
        try:
            active = time.time() - path.stat().st_mtime < 10
        except OSError:
            active = False
        wake.wait(2 if active else 15)


if __name__ == "__main__":
    main()
