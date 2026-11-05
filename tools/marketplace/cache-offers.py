"""Assemble private read-only catalogue exports for the in-game draft browser.

This is a local preview bridge, not an exchange, Vault service or purchase API.
"""
import argparse
import hashlib
import json
from pathlib import Path
import os
import tempfile
import time


def assemble(sources, now):
    result = None
    sellers = set()
    for seller, path in sources:
        if not seller or len(seller.encode("utf-8")) > 128 or any(ord(c) < 32 or ord(c) == 127 for c in seller) or seller in sellers:
            raise ValueError("Each seller must have a distinct, printable name of at most 128 bytes.")
        sellers.add(seller)
        if path.stat().st_size > 16 * 1024 * 1024:
            raise ValueError("Catalogue exceeds 16 MiB.")
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        if data.get("schema") != 1 or data.get("game_mode") != "2.4" or type(data.get("hardcore")) is not bool or not isinstance(data.get("mod"), str):
            raise ValueError("Unsupported catalogue game mode.")
        if data.get("custody") != "local_save" or data.get("purchasable") is not False:
            raise ValueError("Only read-only local-save catalogues are supported.")
        captured = data.get("captured_at")
        if type(captured) is not int or captured > now + 60 or captured + 3600 <= now:
            raise ValueError("Catalogue is missing its capture time or is over one hour old. Rescan it.")
        if result is None:
            result = {"schema": 1, "game_mode": "2.4", "mod": data["mod"], "hardcore": data["hardcore"], "expires_at": now + 3600, "listings": []}
        if data["mod"] != result["mod"] or data["hardcore"] != result["hardcore"]:
            raise ValueError("Do not mix mods or hardcore/softcore catalogues.")
        result["expires_at"] = min(result["expires_at"], captured + 3600)
        items = data.get("items")
        if not isinstance(items, list) or len(items) > 4096:
            raise ValueError("Invalid catalogue item list.")
        ids = set()
        for item in items:
            price = item.get("price")
            if price is None:
                continue
            if not isinstance(price, list) or not 1 <= len(price) <= 16:
                raise ValueError("Invalid price bundle.")
            raw = item.get("item_hex", "")
            if not isinstance(raw, str) or not 0 < len(raw) <= 65536 or len(raw) % 2 or any(c not in "0123456789abcdef" for c in raw):
                raise ValueError("Invalid serialized listing item.")
            tab = item.get("tab")
            if type(tab) is not int or not 1 <= tab <= 7:
                raise ValueError("Invalid shared tab.")
            identity = hashlib.sha256(f"{tab}:{raw}".encode()).hexdigest()
            if identity in ids:
                raise ValueError("Duplicate catalogue item.")
            ids.add(identity)
            revision = hashlib.sha256(json.dumps(price, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            result["listings"].append({"seller": seller, "id": identity, "revision": revision, "item_hex": raw, "price": price})
            if len(result["listings"]) > 4096:
                raise ValueError("Too many offers for the preview cache.")
    if result is None:
        raise ValueError("At least one catalogue is required.")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seller", nargs=2, action="append", required=True, metavar=("NAME", "CATALOGUE"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        now = int(time.time())
        result = assemble([(name, Path(path)) for name, path in args.seller], now)
        expected = "MarketplaceFeed_2.4_" + ("HC" if result["hardcore"] else "SC") + ".json"
        if args.output.name != expected:
            raise ValueError("Output must be named " + expected)
        # The HUD either sees the old complete cache or the new complete cache.
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=args.output.parent, delete=False) as file:
                temporary = Path(file.name)
                json.dump(result, file, indent=2)
                file.write("\n")
                file.flush()
                os.fsync(file.fileno())
            os.replace(temporary, args.output)
        finally:
            if temporary and temporary.exists():
                temporary.unlink()
        print(f"Cached {len(result['listings'])} draft offer(s); no saves or items changed.")
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Marketplace cache: {error}\n")


if __name__ == "__main__":
    main()
