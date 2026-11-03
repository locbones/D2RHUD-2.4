"""Real-item copy rehearsal with actual process exits at durable write boundaries."""
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import uuid

SOURCE = Path(__file__).resolve().parents[1] / "tools/marketplace/custody-workspace.py"
spec = importlib.util.spec_from_file_location("custody", SOURCE)
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def refuses(call):
    try:
        call()
    except (OSError, ValueError, KeyError):
        return
    raise AssertionError("Unsafe operation was accepted")


def empty_page(raw):
    result, at = bytearray(), 0
    for _ in range(7):
        header = bytearray(raw[at:at+68])
        at += struct.unpack_from("<I", header, 16)[0]
        struct.pack_into("<I", header, 12, 0)
        struct.pack_into("<I", header, 16, 68)
        struct.pack_into("<H", header, 66, 0)
        result.extend(header)
    assert at == len(raw)
    return bytes(result)


def crash(operation, workspace, helper, data, request, point, index):
    code = """import runpy,sys,os,json
m=runpy.run_path(sys.argv[1])
def fault(point,index):
 if point==sys.argv[7] and index==int(sys.argv[8]):os._exit(77)
request=json.load(open(sys.argv[6],encoding='utf-8'))
if sys.argv[2]=='prepare':m['prepare'](sys.argv[3],sys.argv[4],sys.argv[5],request,fault=fault)
else:m['apply'](sys.argv[3],request['id'],fault=fault)
"""
    file = workspace.parent / (workspace.name + "-request.json")
    c.save(file, request)
    child = subprocess.run([sys.executable, "-c", code, str(SOURCE), operation, str(workspace), str(helper), str(data), str(file), point, str(index)], capture_output=True)
    assert child.returncode == 77, child.stderr.decode(errors="replace")


def main():
    helper, data, saves, output = map(lambda p: Path(p).resolve(), sys.argv[1:])
    source_hashes = {p: c.digest(c.read(p)) for p in saves.iterdir() if p.suffix in {".d2i", ".d2s"}}
    # Select only a parser-supported real page; every source is opened read-only.
    candidate = None
    for path in sorted(saves.glob("Stash_SC_Page*.d2i")):
        try:
            inv = c.run_helper(helper, data, "inventory", path)
        except ValueError:
            continue
        if inv["items"]:
            candidate = path, inv
            if path.name == "Stash_SC_Page11.d2i":
                break
    assert candidate, "Need one complete real page for read-only rehearsal"
    original, inv = candidate
    raw = bytearray(c.read(original))
    # Controlled fixture gold, while retaining every real serialized item.
    at = 0
    for _ in range(7):
        struct.pack_into("<I", raw, at+12, 100)
        at += struct.unpack_from("<I", raw, at+16)[0]
    with tempfile.TemporaryDirectory(prefix="custody-fixture-") as temporary:
        fixture = Path(temporary) / "source"
        fixture.mkdir()
        (fixture / "Stash_SC_Page11.d2i").write_bytes(raw)
        (fixture / "Stash_SC_Page2.d2i").write_bytes(empty_page(raw))
        settings = {"schema": 1, "game_mode": "2.4", "mod": "RMD-MP", "hardcore": False,
                    "enabled": True, "page": 11, "withdraw_page": 2, "auto_trade": False,
                    "drafts": [], "agreements": [], "simulation": {"enabled": False, "page": 3, "drafts": []}}
        c.save(fixture / "Marketplace_2.4_SC.json", settings)
        item = next((i for i in inv["items"] if i["code"] == "rin"), inv["items"][0])
        gold = {"code": "gld", "label": "Gold", "quality": 2, "index": -1, "quantity": 10, "ethereal": -1, "kind": "gold"}
        request = {"schema": 1, "id": uuid.uuid4().hex, "hardcore": False, "edits": [
            {"page": 11, "take": [{k: item[k] for k in ("tab", "ordinal", "item_hex")}], "debit": [gold]},
            {"page": 2, "credit_items": [{"item_hex": item["item_hex"]}], "credit_balances": [gold], "gold_cap": 2500000}]}
        output.mkdir(parents=True, exist_ok=True)
        lab = output / ("run-" + uuid.uuid4().hex)
        lab.mkdir()
        base = lab / "clean"
        c.create(fixture, base)
        before = {n: c.read(base/n) for n in ("Stash_SC_Page11.d2i", "Stash_SC_Page2.d2i")}
        journal = c.prepare(base, helper, data, request)
        assert journal["state"] == "prepared" and all(c.read(base/n) == raw for n, raw in before.items())
        assert c.prepare(base, helper, data, request) == journal
        reused = json.loads(json.dumps(request)); reused["edits"][0]["debit"][0]["quantity"] = 11
        refuses(lambda: c.prepare(base, helper, data, reused))
        new_id = dict(request, id=uuid.uuid4().hex)
        refuses(lambda: c.prepare(base, helper, data, new_id))
        assert c.apply(base, request["id"])["state"] == "applied"
        after = {n: c.read(base/n) for n in before}
        assert c.apply(base, request["id"])["state"] == "applied" and all(c.read(base/n) == raw for n, raw in after.items())
        debit = c.run_helper(helper, data, "inventory", base/"Stash_SC_Page11.d2i")
        credit = c.run_helper(helper, data, "inventory", base/"Stash_SC_Page2.d2i")
        assert len(debit["items"]) + len(credit["items"]) == len(inv["items"])
        assert debit["gold"] == 690 and credit["gold"] == 10
        moved = credit["items"][0]
        for key in ("code", "saved_id", "quality", "index", "saved_properties", "socketed_items"):
            assert moved[key] == item[key], key
        assert all(c.digest(c.read(p)) == h for p, h in source_hashes.items())
        # A later legitimate commit must not make retrying an old receipt replay it.
        second = {"schema": 1, "id": uuid.uuid4().hex, "hardcore": False,
                  "edits": [{"page": 11, "debit": [dict(gold, quantity=1)]}]}
        c.prepare(base, helper, data, second); c.apply(base, second["id"])
        assert c.apply(base, request["id"])["state"] == "applied"
        # Actual abrupt process exits, including between two page replacements.
        for n, (op, point, index) in enumerate([("prepare", "prepared_image", 0), ("apply", "applying", -1), ("apply", "replaced", 0), ("apply", "replaced", 1)]):
            work = lab / f"crash-{n}"
            c.create(fixture, work)
            if op == "apply":
                c.prepare(work, helper, data, request)
            crash(op, work, helper, data, request, point, index)
            if op == "prepare":
                c.prepare(work, helper, data, request)
            assert c.apply(work, request["id"])["state"] == "applied"
            assert all(c.read(work/name) == raw for name, raw in after.items())
        bad = lab / "bad-backup"; c.create(fixture, bad); c.prepare(bad, helper, data, request)
        (bad / "transactions" / request["id"] / "after-0.bin").write_bytes(b"corrupt")
        refuses(lambda: c.apply(bad, request["id"]))
        assert all(c.read(bad/n) == raw for n, raw in before.items())
        restored = lab / "restored"; c.create(fixture, restored); c.prepare(restored, helper, data, request); c.apply(restored, request["id"])
        (restored / "Stash_SC_Page11.d2i").write_bytes(before["Stash_SC_Page11.d2i"])
        refuses(lambda: c.apply(restored, request["id"]))
        assert c.document(restored / "transactions" / request["id"] / "journal.json")["state"] == "conflict"
        refuses(lambda: c.prepare(restored, helper, data, new_id))
        failed = lab / "failed-plan"; c.create(fixture, failed)
        insufficient = dict(request, id=uuid.uuid4().hex, edits=[{"page": 11, "debit": [dict(gold, quantity=701)]}])
        refuses(lambda: c.prepare(failed, helper, data, insufficient))
        assert not (failed/"transactions").exists() and all(c.read(failed/n) == raw for n, raw in before.items())
        wrong_route = dict(request, id=uuid.uuid4().hex, edits=[{"page": 2, "debit": [gold]}])
        refuses(lambda: c.prepare(failed, helper, data, wrong_route))
        no_room = dict(request, id=uuid.uuid4().hex, edits=[request["edits"][0], dict(request["edits"][1], gold_cap=1)])
        refuses(lambda: c.prepare(failed, helper, data, no_room))
        assert all(c.read(failed/n) == raw for n, raw in before.items())
        pinned = lab / "pinned-data"
        for name, hashed in c.compatibility(data).items():
            if hashed is not None:
                file = pinned / name; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(c.read(data/name, 64*1024*1024))
        versioned = lab / "version-change"; c.create(fixture, versioned); c.prepare(versioned, helper, pinned, request)
        misc = pinned / "global/excel/misc.bin"; original_table = c.read(misc); changed_table = bytearray(original_table); changed_table[-1] ^= 1; misc.write_bytes(changed_table)
        refuses(lambda: c.apply(versioned, request["id"]))
        assert all(c.read(versioned/n) == raw for n, raw in before.items())
        misc.write_bytes(original_table); assert c.apply(versioned, request["id"])["state"] == "applied"
        wrong_mode = dict(request, id=uuid.uuid4().hex, hardcore=True)
        refuses(lambda: c.prepare(failed, helper, data, wrong_mode))
        with c.lease(failed):
            refuses(lambda: c.prepare(failed, helper, data, request))
        refuses(lambda: c.transaction(failed, "../outside"))
        refuses(lambda: c.create(fixture, fixture / "nested"))
        assert all(c.digest(c.read(p)) == h for p, h in source_hashes.items())
        report = {"simulation": True, "source_page": original.name, "item_code": item["code"],
                  "process_crash_points": 4, "source_saves_unchanged": len(source_hashes),
                  "custody_workspace": str(lab), "network_requests": 0, "completed_sales": 0}
        c.save(output / "latest-rehearsal.json", report)
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
