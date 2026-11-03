"""Guarded escrow agent. Requires enrollment and the launcher/HUD file leases.

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
import urllib.error
import urllib.parse
import urllib.request
import uuid

spec = importlib.util.spec_from_file_location("custody_io", Path(__file__).with_name("custody-workspace.py"))
io = importlib.util.module_from_spec(spec)
spec.loader.exec_module(io)


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
        self.transport = transport
        self.fault = fault or (lambda stage: None)
        self.verify()

    def verify(self):
        if io.digest(io.read(self.helper)) != self.config["helper_sha256"] or catalog(self.data) != self.config["catalog"]:
            raise ValueError("Save helper or compiled catalog changed; custody is paused for revalidation")
        if not self.test:
            binaries = [(Path(self.config["game_directory"]) / "d2rhud.dll", self.config["hud_sha256"]),
                        (Path(self.config["launcher_path"]), self.config["launcher_sha256"])]
            for path, expected in binaries:
                if io.digest(io.read(path, 256 * 1024 * 1024)).lower() != expected.lower():
                    raise ValueError("The guarded launcher or HUD changed; revalidate custody before writing saves")

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
        if (window.get('deposit') != settings['page'] or window.get('delivery') != settings['withdraw_page'] or
                type(window.get('loaded')) is not int or not 1 <= window['loaded'] <= 64 or
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

    def live_result(self, window, error=None):
        if window:
            io.save(self.root / 'live-result.json', dict(schema=1, session=window['session'], at=int(time.time()),
                    terminal=True, pending=self.pending.exists(), error=str(error)[:512] if error else ''))

    def request(self, mode, data):
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
        if type(deposit) is not int or type(delivery) is not int or not 1 <= deposit <= 64 or not 1 <= delivery <= 64 or deposit == delivery:
            raise ValueError("Choose different valid deposit and delivery pages")
        return value

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
            io.save(self.pending, dict(id=identity, mode=mode))
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
        try:
            ticket = self.request(mode, record["request"])
        except ValueError as error:
            if record["request"]["type"] == "claim_prepare" and not record["images"] and "409: No unlisted escrow assets" in str(error):
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
                    credit_items=ticket["packet"]["items"], credit_balances=ticket["packet"]["balances"])
                io.atomic(directory / f"{page}.before", before)
                io.atomic(directory / f"{page}.after", after)
                record["images"] = [dict(page=page, before=io.digest(before), after=io.digest(after))]
                record["delivered"] = [dict(item=item["item"], item_hex=placed["item_hex"])
                    for item, placed in zip(ticket["packet"]["items"], result["delivered"], strict=True)]
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
        with self.save_access(mode, [image["page"] for image in record["images"]]):
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
            for image in record["images"]:
                state["consumed"][record["mode"] + ":" + str(image["page"]) + ":" + image["before"]] = record["id"]
            for item in record.get("delivered", []):
                state["delivered"][item["item"]] = dict(mode=record["mode"], item_hex=item["item_hex"])
            for item in record["request"].get("items", []):
                state["delivered"].pop(item.get("ledger_id"), None)
            state["receipts"].append(record["id"])
            io.save(state_path, state)
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
            raise ValueError('Invalid wallet withdrawal queue')
        self.delivery_items[mode] = deliveries
        self.wallet_withdrawals[mode] = withdrawals

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
                    price = next((d['price'] for d in settings['drafts'] if d['tab'] == row['tab'] and d['item_hex'] == row['item_hex']), None)
                    if price:
                        priced.append(dict(row, price=price))
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
            response = self.request(mode, dict(type="web_sync", settings={key: settings[key] for key in ("page", "withdraw_page", "enabled", "auto_trade")}, inventory=rows, acknowledged=acknowledgements))
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
                    elif intent["kind"] == "price":
                        if current["page"] != packet["page"] or not any(row["tab"] == packet["tab"] and row["item_hex"] == packet["item_hex"] for row in self.inventory(self.page(mode, current["page"]))["items"]):
                            raise ValueError("The stash item moved or changed before its asking price could be applied")
                        current["drafts"] = [row for row in current["drafts"] if not (row["tab"] == packet["tab"] and row["item_hex"] == packet["item_hex"])]
                        if packet["price"]:
                            current["drafts"].append({key: packet[key] for key in ("tab", "item_hex", "price")})
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
                    if type(current["page"]) is not int or type(current["withdraw_page"]) is not int or not 1 <= current["page"] <= 64 or not 1 <= current["withdraw_page"] <= 64 or current["page"] == current["withdraw_page"]:
                        raise ValueError("Choose different valid stash pages")
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
            self.request(mode, dict(type="web_sync", settings={key: settings[key] for key in ("page", "withdraw_page", "enabled", "auto_trade")}, inventory=rows, acknowledged=acknowledgements))

    def deposit(self, mode):
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
        for row in inventory["items"]:
            price = next((d["price"] for d in settings["drafts"] if d["tab"] == row["tab"] and d["item_hex"] == row["item_hex"]), None)
            if len(items) < 64 and (price or row["identified"] and row["quality"] in {2, 5, 7} and row["code"] != "Z01"):
                item = self.web_item(row, page)
                if price:
                    item["price"] = price
                lineage = [key for key, value in state["delivered"].items() if value["mode"] == mode and positionless(value["item_hex"]) == positionless(row["item_hex"])]
                if len(lineage) > 1:
                    raise ValueError("Ambiguous item lineage; do not merge identical seeds or bytes")
                if lineage:
                    item["ledger_id"] = lineage[0]
                items.append(item)
                selectors.append({k: row[k] for k in ("tab", "ordinal", "item_hex")})
            elif not price and row["code"] == "Z01" and row["quality"] == 2:
                for balance in row["balances"]:
                    resources[balance["stat"]] = min(2000000000, resources.get(balance["stat"], 0) + balance["quantity"])
        balances = [dict(kind="bag", code="Z01", label=stat, stat=stat, quality=2, index=-1, ethereal=-1, quantity=q) for stat, q in sorted(resources.items()) if q][:15]
        if inventory["gold"]:
            balances.append(dict(kind="gold", code="gld", label="Gold", quality=2, index=-1, ethereal=-1, quantity=min(inventory["gold"], 2000000000)))
        if not items and not balances:
            return False
        raw, after, result = self.plan(mode, settings, page, take=selectors, debit=balances)
        if raw != before:
            raise ValueError("Deposit page changed before planning")
        request = dict(id=uuid.uuid4().hex, type="deposit_prepare", items=items, balances=balances,
                       pages=[dict(page=page, before=io.digest(before), after=io.digest(after))])
        self.start(mode, request, [(page, before, after)])
        return True

    def claim(self, mode, item_limit=64, item_ids=None, withdrawal_id=None):
        try:
            settings = self.settings(mode)
            page = self.page(mode, settings["withdraw_page"])
            inventory = self.inventory(page)
        except BlockingIOError:
            raise
        except (OSError, ValueError, KeyError) as error:
            raise CapacityError("Delivery page is not ready: " + str(error), shrink=False) from error
        capacities = {}
        for row in inventory["items"]:
            for cap in row["credit_capacity"]:
                capacities[cap["stat"]] = min(2000000000, capacities.get(cap["stat"], 0) + cap["quantity"])
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
                raise CapacityError("Make room for gold or place a compatible Storage Bag on your delivery page.", shrink=False)
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
                message = 'Make room for gold or place a compatible Storage Bag with free capacity on your delivery page. The remaining amount stays reserved.'
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

    def run(self, preflight=False):
        with lease(self.root / "guard.lock"):
            with self.save_access(pages=[]) as window:
                live_mode = ("HC" if window["hardcore"] else "SC") if window else None
            rejected = {}
            try:
                self.recover()
            except DepositRejected as error:
                rejected['recovered'] = str(error)
            results = {}
            status_path = self.root / "status.json"
            warnings = io.document(status_path).get("delivery_warnings", {}) if status_path.exists() else {}
            deliveries = {}
            for mode in ("SC", "HC"):
                if live_mode is not None and mode != live_mode:
                    continue
                if not (self.folder / f"Marketplace_2.4_{mode}.json").exists():
                    continue
                try:
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
                    if self.delivery_items.get(mode):
                        try:
                            delivered = self.collect_deliveries(mode, self.delivery_items[mode])
                        except BlockingIOError:
                            delivered = 'Switch away from your delivery page to receive queued trades.'
                        deliveries[mode] = delivered
                        if isinstance(delivered, str):
                            warnings[mode] = delivered
                        else:
                            warnings.pop(mode, None)
                try:
                    self.collect_wallet_withdrawals(mode)
                    if warnings.get(mode) == 'Switch away from your delivery page to receive queued withdrawals.':
                        warnings.pop(mode, None)
                except BlockingIOError:
                    warnings[mode] = 'Switch away from your delivery page to receive queued withdrawals.'
            if preflight:
                for mode, value in results.items():
                    if isinstance(value, str):
                        warnings[mode] = value
                    else:
                        warnings.pop(mode, None)
            io.save(status_path, dict(at=int(time.time()), pending=self.pending.exists(), result=results, deliveries=deliveries, delivery_warnings=warnings, rejected_deposits=rejected))
            self.live_result(window)
            return results


def report_failure(config, error):
    """Surface even constructor/verification failures; never authorize or write saves."""
    root = Path(config['state_directory'])
    status_path = root / 'status.json'
    status = io.document(status_path) if status_path.exists() else {}
    message = str(error)[:512]
    status.update(at=int(time.time()), error=message, pending=(root / 'pending.json').exists())
    io.save(status_path, status)
    try:
        window = io.document(root / 'live-window.json')
        if (window.get('owner') == config.get('owner') and window.get('catalog') == config.get('catalog')
                and isinstance(window.get('session'), str)):
            io.save(root / 'live-result.json', dict(schema=1, session=window['session'], at=int(time.time()),
                terminal=True, pending=status['pending'], error=message))
    except (OSError, ValueError):
        pass


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
    while True:
        agent = None
        config = {}
        try:
            config = io.document(args.config)
            agent = Agent(config)
            if args.watch:
                io.save(agent.root / 'live-capability.json', dict(schema=1, owner=agent.config['owner'], catalog=agent.config['catalog']))
            if not args.preflight and time.monotonic() - last_link >= 15:
                last_link = time.monotonic()
                try:
                    agent.publish_browser_link()
                except (OSError, ValueError) as error:
                    # Website linking is optional; it cannot stop custody or
                    # create a pending save operation when the site is offline.
                    print("Browser link: " + str(error), flush=True)
            if args.watch and time.monotonic() - last_profiles >= 60:
                last_profiles = time.monotonic()
                try:
                    agent.publish_characters()
                except (OSError, ValueError, KeyError) as error:
                    print('Character profiles: ' + str(error), flush=True)
            delivery_request = agent.root / 'delivery-request.json'
            if args.watch and delivery_request.exists() and not game_running():
                requested = io.read(delivery_request)
                print(json.dumps(agent.run(preflight=True)), flush=True)
                if delivery_request.exists() and io.read(delivery_request) == requested:
                    delivery_request.unlink()
            print(json.dumps(agent.run(args.preflight)), flush=True)
        except (OSError, ValueError, KeyError) as error:
            if args.watch and config:
                try:
                    report_failure(config, error)
                except (OSError, ValueError, KeyError):
                    pass
            if not args.watch:
                raise SystemExit(str(error)) from None
            print(str(error), flush=True)
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
