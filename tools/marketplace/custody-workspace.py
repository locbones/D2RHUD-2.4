"""Prepare/recover custody transactions in disposable save COPIES only.

No network access, Vault acknowledgement, game launch or live save writes.
This is the recovery rehearsal for the future closed-game custody agent.
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import tempfile
import time
import uuid

LIMIT = 32 * 1024 * 1024
MARKER = "custody-simulation.json"
HUD_FEATURES = {name: 1 for name in ('open_offers', 'torch_class', 'listing_comments', 'gift_resources')}
_HUD_CONTRACTS = {}

class BackupSaveDirectory(ValueError):
    pass


def active_save_directory(path):
    folder = Path(path)
    resolved = folder.resolve()
    if any(part.casefold() == 'backups' for route in (folder, resolved) for part in route.parts):
        raise BackupSaveDirectory('Marketplace is linked to a backup folder. Open Pandemonium with the live save folder selected so it can reconnect after the game closes.')
    return resolved


def hud_contract(path):
    """Read a bounded, exported data declaration; never load/execute the DLL.

    Cache by file identity so normal sync polls do not repeatedly read a large
    HUD. A replacement/change invalidates the cache, even at the same path.
    """
    path = Path(path).resolve()
    def identity():
        stat = path.stat()
        return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns
    before = identity()
    old = _HUD_CONTRACTS.get(path)
    if old and old[0] == before:
        return old[1]
    raw = read(path, 256 * 1024 * 1024)
    contract = parse_hud_contract(raw)
    if before != identity():
        raise BlockingIOError('The HUD is updating; marketplace sync will retry automatically')
    if len(_HUD_CONTRACTS) >= 32:
        _HUD_CONTRACTS.clear()
    _HUD_CONTRACTS[path] = before, contract
    return contract


def parse_hud_contract(raw):
    """Legacy HUDs have no export. Malformed/new contracts fail closed."""
    if not raw.startswith(b'MZ'):
        return None  # Includes old, non-executed compatibility test fixtures.
    def value(fmt, at):
        size = struct.calcsize(fmt)
        if at < 0 or at > len(raw) - size:
            raise ValueError('The HUD marketplace declaration is truncated')
        return struct.unpack_from(fmt, raw, at)[0]
    pe = value('<I', 0x3c)
    if raw[pe:pe+4] != b'PE\0\0' or value('<H', pe+4) != 0x8664:
        raise ValueError('The HUD is not a supported Windows x64 DLL')
    count, optional = value('<H', pe+6), value('<H', pe+20)
    if not 1 <= count <= 96 or optional < 120 or value('<H', pe+24) != 0x20b or not value('<H', pe+22) & 0x2000:
        raise ValueError('The HUD has an invalid marketplace PE layout')
    sections = pe+24+optional
    def address(rva, size):
        if rva < 0 or size < 0:
            raise ValueError('Invalid HUD export address')
        for n in range(count):
            section = sections + n*40
            start, length, offset = value('<I', section+12), value('<I', section+16), value('<I', section+20)
            if start <= rva and rva-start <= length and size <= length-(rva-start):
                at = offset+rva-start
                if at <= len(raw)-size:
                    return at
        raise ValueError('The HUD marketplace export points outside the DLL')
    def string(rva, limit):
        data = bytearray()
        for i in range(limit):
            byte = raw[address(rva+i, 1)]
            if not byte:
                return bytes(data)
            data.append(byte)
        raise ValueError('The HUD marketplace export exceeds its size limit')
    export, export_size = value('<I', pe+24+112), value('<I', pe+24+116)
    contract, found = None, False
    if export:
        directory = address(export, 40)
        functions, names = value('<I', directory+20), value('<I', directory+24)
        if functions > 65536 or names > 65536:
            raise ValueError('The HUD has too many exports')
        if not names:
            return None
        function_rva, name_rva, ordinal_rva = (value('<I', directory+n) for n in (28, 32, 36))
        function_at, name_at, ordinal_at = address(function_rva, functions*4), address(name_rva, names*4), address(ordinal_rva, names*2)
        for i in range(names):
            if string(value('<I', name_at+i*4), 256) != b'D2RHUD_MarketplaceProtocol':
                continue
            found = True
            index = value('<H', ordinal_at+i*2)
            if index >= functions:
                raise ValueError('Invalid HUD marketplace export index')
            target = value('<I', function_at+index*4)
            if export <= target < export+export_size:
                raise ValueError('The HUD marketplace declaration cannot be forwarded')
            try:
                def unique(pairs):
                    result = {}
                    for key, item in pairs:
                        if key in result:
                            raise ValueError('Duplicate HUD marketplace declaration key')
                        result[key] = item
                    return result
                contract = json.loads(string(target, 4096), object_pairs_hook=unique)
            except (UnicodeError, ValueError, RecursionError) as error:
                raise ValueError('The HUD marketplace declaration is invalid: ' + str(error)) from None
            break
    if not found:
        return None
    if (not isinstance(contract, dict) or type(contract.get('schema')) is not int or contract['schema'] != 1 or
            contract.get('game_mode') != '2.4' or type(contract.get('live_custody')) is not int or contract['live_custody'] != 2):
        raise ValueError('This HUD uses an unsupported marketplace protocol; the companion needs a compatible update. Your account and Vault are preserved')
    features = contract.get('features')
    if (not isinstance(features, dict) or len(features) > 32 or any(not re.fullmatch('[a-z][a-z0-9_]{0,63}', key) or
            type(version) is not int or not 1 <= version <= 255 for key, version in features.items())):
        raise ValueError('The HUD marketplace feature declaration is invalid')
    return contract


def hud_features(config, legacy):
    if config.get('game_directory'):
        contract = hud_contract(Path(config['game_directory']) / 'd2rhud.dll')
        if contract is not None:
            return [name for name, version in HUD_FEATURES.items() if contract['features'].get(name) == version]
    # Previously released HUDs remain supported during the transition.
    return [name for name in HUD_FEATURES if config.get('hud_sha256') in legacy.get(name+'_hud_sha256', [])]


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def read(path, limit=LIMIT):
    with Path(path).open("rb") as stream:
        raw = stream.read(limit + 1)
    if len(raw) > limit:
        raise ValueError("Custody input exceeds its size limit")
    return raw


def read_sidecar(path, limit=LIMIT):
    """Read an atomic metadata snapshot without blocking replacement on Windows."""
    if os.name != "nt":
        return read(path, limit)
    import ctypes
    import msvcrt
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    create.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    # GENERIC_READ, FILE_SHARE_READ | WRITE | DELETE, OPEN_EXISTING.
    handle = create(str(path), 0x80000000, 7, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        kernel.CloseHandle(handle)
        raise
    with os.fdopen(fd, "rb") as stream:
        raw = stream.read(limit + 1)
    if len(raw) > limit:
        raise ValueError("Custody input exceeds its size limit")
    return raw


def document(path, limit=LIMIT):
    return json.loads(read_sidecar(path, limit).decode("utf-8-sig"))


def is_contention(error):
    return isinstance(error, BlockingIOError) or getattr(error, "winerror", None) in (32, 33)


def replace(source, target):
    if os.name == "nt":
        import ctypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        move = kernel.MoveFileExW
        move.argtypes = (ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32)
        move.restype = ctypes.c_int
        if not move(str(source), str(target), 0x1 | 0x8):  # REPLACE_EXISTING | WRITE_THROUGH
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        os.replace(source, target)


def atomic(path, raw, *, sidecar=False):
    path = Path(path)
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
        # Older readers may still hold a short-lived handle without delete
        # sharing. Retry metadata replacement only, never a game save write.
        for attempt in range(4):
            try:
                replace(temporary, path)
                break
            except OSError as error:
                if not sidecar or not is_contention(error) or attempt == 3:
                    raise
                time.sleep(.02 * (attempt + 1))
        if os.name != "nt":
            fd = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
    finally:
        temporary.unlink(missing_ok=True)


def save(path, value):
    atomic(path, (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode(), sidecar=True)


def create(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("Use a separate, new simulation directory outside the save folder")
    if destination.exists():
        raise ValueError("Simulation destination already exists")
    destination.mkdir(parents=True, mode=0o700)
    copied = {}
    for path in sorted(source.iterdir()):
        if not re.fullmatch(r"Stash_(SC|HC)_Page([1-9]|[1-5][0-9]|6[0-4])\.d2i|Marketplace_2\.4_(SC|HC)\.json", path.name):
            continue
        raw = read(path)
        if read(path) != raw:
            raise ValueError("A source save changed during the copy; retry after saving finishes")
        atomic(destination / path.name, raw)
        copied[path.name] = digest(raw)
    if not any(name.endswith(".d2i") for name in copied):
        raise ValueError("No numbered stash pages were found")
    save(destination / MARKER, {"schema": 1, "simulation": True, "id": uuid.uuid4().hex,
        "source_directory": str(source), "files": copied})
    return {"workspace": str(destination), "copied": len(copied), "simulation": True}


def workspace(folder):
    root = Path(folder).resolve()
    marker = document(root / MARKER)
    source = Path(marker["source_directory"]).resolve()
    if marker.get("schema") != 1 or marker.get("simulation") is not True or root == source or root in source.parents or source in root.parents:
        raise ValueError("This tool only writes isolated simulation workspaces")
    # Even a manually created marker must not turn this into a live-save writer.
    if any(part.casefold() == "diablo ii resurrected" for part in root.parts):
        raise ValueError("A simulation workspace cannot be a Diablo II save directory")
    return root


def inside(root, relative):
    path = (root / relative).resolve()
    if not path.is_relative_to(root) or path == root:
        raise ValueError("Custody path escapes the simulation directory")
    return path


@contextmanager
def lease(root):
    with inside(root, ".custody.lock").open("a+b") as stream:
        if stream.tell() == 0:
            stream.write(b"0")
            stream.flush()
        stream.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            if os.name == "nt":
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def transaction(root, identity):
    if not isinstance(identity, str) or not re.fullmatch("[0-9a-f]{32}", identity):
        raise ValueError("Use a stable 32-character transaction ID")
    return inside(root, "transactions/" + identity)


def run_helper(helper, data, command, path):
    try:
        result = subprocess.run([str(Path(helper).resolve()), command, str(Path(data).resolve()), str(path)],
                                capture_output=True, check=False, timeout=30,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except subprocess.TimeoutExpired as error:
        raise ValueError("Custody planner timed out; no page was written") from error
    if result.returncode:
        raise ValueError(result.stderr.decode("utf-8", errors="replace").strip())
    if len(result.stdout) > LIMIT:
        raise ValueError("Custody helper response exceeds its size limit")
    return json.loads(result.stdout)


def mod_name(data):
    package = Path(data).resolve().parent
    name = package.stem
    if package.suffix.lower() != '.mpq' or package.parent.name != name or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._ -]{0,63}', name):
        raise ValueError('Mod data must belong to its matching installed mod package')
    return name


def compatibility(data):
    root = Path(data).resolve()
    rows = {f"global/excel/{name}.bin": digest(read(root / f"global/excel/{name}.bin", 64 * 1024 * 1024))
            for name in ("weapons", "armor", "misc", "itemstatcost", "itemtypes")}
    name = "D2RHUD/table-metadata.json"
    rows[name] = digest(read(root / name)) if (root / name).exists() else None
    return rows


def save_catalog(data, helper):
    result = subprocess.run([str(Path(helper).resolve()), 'save-catalog', str(Path(data).resolve()), 'unused'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    raw = result.stdout.strip()
    if result.returncode or len(raw) > 2 * 1024 * 1024:
        raise ValueError('Could not read the mod save-format description: ' + result.stderr.decode('utf-8', errors='replace')[:512])
    value = json.loads(raw)
    if value.get('schema') != 2 or value.get('game_mode') != '2.4':
        raise ValueError('Unsupported marketplace save-format description')
    return digest(raw), value


def history(root):
    records = []
    directory = inside(root, "transactions")
    if directory.exists():
        for path in directory.iterdir():
            if re.fullmatch("[0-9a-f]{32}", path.name):
                row = document(inside(root, f"transactions/{path.name}/journal.json"))
                if row.get("id") != path.name or type(row.get("sequence")) is not int:
                    raise ValueError("Invalid transaction history")
                records.append(row)
    records.sort(key=lambda r: r["sequence"])
    heads = document(root / MARKER)["files"].copy()
    for n, row in enumerate(records, 1):
        if row["sequence"] != n or row["state"] not in {"prepared", "applying", "applied"} or (row["state"] != "applied" and n != len(records)):
            raise ValueError("Reconcile the incomplete custody transaction before continuing")
        for file in row["files"]:
            if heads.get(file["name"]) != file["before"]:
                raise ValueError("Custody transaction history diverged")
            if row["state"] == "applied":
                heads[file["name"]] = file["after"]
    return records, heads


def prepare(folder, helper, data, request, fault=None):
    root = workspace(folder)
    identity = request["id"]
    tx = transaction(root, identity)
    if request.get("schema") != 1 or type(request.get("hardcore")) is not bool:
        raise ValueError("Invalid custody rehearsal request")
    request_hash = digest(json.dumps(request, sort_keys=True, separators=(",", ":")).encode())
    mode = "HC" if request["hardcore"] else "SC"
    with lease(root):
        if tx.exists():
            journal = document(tx / "journal.json")
            if journal["request_hash"] != request_hash:
                raise ValueError("Transaction ID reused with different contents")
            return journal
        records, heads = history(root)
        if any(row["state"] != "applied" for row in records):
            raise ValueError("Apply or recover the pending transaction before preparing another")
        edits = request["edits"]
        if not isinstance(edits, list) or not 1 <= len(edits) <= 2:
            raise ValueError("Choose one deposit edit and/or one delivery edit")
        settings = document(inside(root, f"Marketplace_2.4_{mode}.json"))
        pinned_tables = compatibility(data)
        planner_hash = digest(read(helper, 64 * 1024 * 1024))
        plans, pages = [], set()
        # Plan every page before creating any transaction or changing a copy.
        with tempfile.TemporaryDirectory(prefix="custody-plan-") as temporary:
            for edit in edits:
                page = edit["page"]
                if type(page) is not int or not 1 <= page <= 64 or page in pages:
                    raise ValueError("Choose distinct numbered pages from 1 to 64")
                pages.add(page)
                debit = bool(edit.get("take") or edit.get("debit"))
                credit = bool(edit.get("credit_items") or edit.get("credit_balances"))
                if (debit and (page != settings["page"] or settings["enabled"] is not True)) or (credit and page != settings["withdraw_page"]) or not (debit or credit):
                    raise ValueError("Debits require the enabled deposit; credits require the delivery page")
                name = f"Stash_{mode}_Page{page}.d2i"
                before = read(inside(root, name), 8 * 1024 * 1024)
                if digest(before) != heads.get(name):
                    raise ValueError("A copied page changed outside custody; create a new rehearsal workspace")
                payload = {k: edit[k] for k in ("take", "debit", "credit_items", "credit_balances", "gold_cap") if k in edit}
                payload.update(schema=1, game_mode="2.4", mod=mod_name(data), hardcore=request["hardcore"], settings=settings, source_hex=before.hex())
                input_path = Path(temporary) / "request.json"
                save(input_path, payload)
                plan = run_helper(helper, data, "plan", input_path)
                if plan.get("schema") != 1 or plan.get("simulation") is not True:
                    raise ValueError("Unexpected custody helper response")
                after = bytes.fromhex(plan.pop("after_hex"))
                plans.append((name, before, after, plan))
        if compatibility(data) != pinned_tables or digest(read(helper, 64 * 1024 * 1024)) != planner_hash:
            raise ValueError("Planner or mod tables changed during preparation")
        stage = inside(root, "transactions/.preparing-" + identity)
        stage.mkdir(parents=True, exist_ok=True)
        if (stage / "intent.json").exists() and document(stage / "intent.json")["request_hash"] != request_hash:
            raise ValueError("Transaction ID reused during an interrupted preparation")
        save(stage / "intent.json", {"request_hash": request_hash})
        journal = {"schema": 1, "simulation": True, "id": identity, "state": "prepared",
                   "request_hash": request_hash, "sequence": len(records) + 1, "files": [], "settled": False,
                   "mod_data": str(Path(data).resolve()), "compatibility": pinned_tables,
                   "planner": str(Path(helper).resolve()), "planner_sha256": planner_hash}
        for n, (name, before, after, plan) in enumerate(plans):
            atomic(stage / f"before-{n}.bin", before)
            atomic(stage / f"after-{n}.bin", after)
            journal["files"].append({"name": name, "before": digest(before), "after": digest(after), "plan": plan})
            if fault:
                fault("prepared_image", n)
        save(stage / "journal.json", journal)
        replace(stage, tx)
        return journal


def apply(folder, identity, fault=None):
    root = workspace(folder)
    tx = transaction(root, identity)
    with lease(root):
        journal = document(tx / "journal.json")
        _, heads = history(root)
        if journal.get("schema") != 1 or journal.get("simulation") is not True or journal.get("id") != identity or journal.get("settled") is not False:
            raise ValueError("Invalid rehearsal journal")
        if compatibility(journal["mod_data"]) != journal["compatibility"] or digest(read(journal["planner"], 64 * 1024 * 1024)) != journal["planner_sha256"]:
            raise ValueError("Planner or mod tables changed; reconcile this transaction before applying")
        if journal["state"] not in {"prepared", "applying", "applied"}:
            raise ValueError("Transaction needs manual reconciliation")
        pending = []
        for n, row in enumerate(journal["files"]):
            if not re.fullmatch(r"Stash_(SC|HC)_Page([1-9]|[1-5][0-9]|6[0-4])\.d2i", row["name"]):
                raise ValueError("Invalid custody target")
            for prefix in ("before", "after"):
                if digest(read(inside(root, f"transactions/{identity}/{prefix}-{n}.bin"))) != row[prefix]:
                    raise ValueError("A custody backup or prepared image is corrupt")
            current = digest(read(inside(root, row["name"])))
            allowed = {row["before"]} if journal["state"] == "prepared" else {heads[row["name"]]} if journal["state"] == "applied" else {row["before"], row["after"]}
            if current not in allowed:
                journal["state"] = "conflict"
                save(tx / "journal.json", journal)
                raise ValueError("A page changed or was restored outside this transaction; no automatic overwrite")
            if current != row["after"]:
                pending.append((n, row))
        if journal["state"] == "applied":
            return journal
        journal["state"] = "applying"
        save(tx / "journal.json", journal)
        if fault:
            fault("applying", -1)
        for n, row in pending:
            path = inside(root, row["name"])
            if digest(read(path)) != row["before"]:
                raise ValueError("A page changed during apply")
            atomic(path, read(tx / f"after-{n}.bin"))
            if fault:
                fault("replaced", n)
        for row in journal["files"]:
            if digest(read(inside(root, row["name"]))) != row["after"]:
                raise ValueError("Page verification failed; keep the journal for recovery")
        journal["state"] = "applied"
        save(tx / "journal.json", journal)
        return journal


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    new = sub.add_parser("create")
    new.add_argument("source", type=Path)
    new.add_argument("destination", type=Path)
    plan = sub.add_parser("prepare")
    plan.add_argument("workspace", type=Path)
    plan.add_argument("helper", type=Path)
    plan.add_argument("mod_data", type=Path)
    plan.add_argument("request", type=Path)
    for op in ("apply", "recover"):
        child = sub.add_parser(op)
        child.add_argument("workspace", type=Path)
        child.add_argument("id")
    args = parser.parse_args()
    try:
        if args.operation == "create":
            result = create(args.source, args.destination)
        elif args.operation == "prepare":
            result = prepare(args.workspace, args.helper, args.mod_data, document(args.request))
        else:
            result = apply(args.workspace, args.id)
        # Item bytes/backups remain private to the workspace, outside output.
        print(json.dumps({k: v for k, v in result.items() if k != "files"}, indent=2))
    except (OSError, ValueError, KeyError) as error:
        raise SystemExit(str(error))
