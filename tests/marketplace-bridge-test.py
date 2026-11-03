"""Read-only integration audit of the compiled companion against a real page."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import importlib.util


def main():
    scanner, excel, stash = map(Path, sys.argv[1:])
    match = re.fullmatch(r"Stash_(SC|HC)_Page([1-9][0-9]?)\.d2i", stash.name)
    assert match, "Choose an existing numbered 2.4 stash page"
    original_hash = hashlib.sha256(stash.read_bytes()).hexdigest()
    config = {
        "schema": 1, "game_mode": "2.4", "mod": "RMD-MP",
        "hardcore": match[1] == "HC", "enabled": True,
        "page": int(match[2]), "drafts": [],
    }
    with tempfile.TemporaryDirectory(prefix="marketplace-bridge-") as directory:
        settings = Path(directory) / "settings.json"

        def scan(success=True):
            settings.write_text(json.dumps(config), encoding="utf-8")
            result = subprocess.run(
                [str(scanner), str(excel), str(stash), str(settings)],
                capture_output=True, encoding="utf-8", check=False,
            )
            if not success:
                assert result.returncode != 0 and not result.stdout and result.stderr
                return None
            assert result.returncode == 0, result.stderr
            return json.loads(result.stdout)

        snapshot = scan()
        assert snapshot["purchasable"] is False
        assert snapshot["custody"] == "local_save"
        assert snapshot["items"] and snapshot["unmatched_drafts"] == 0
        item = snapshot["items"][0]
        price = [{"code": "r33", "label": "Zod Rune", "quality": 2,
                  "index": -1, "quantity": 40, "ethereal": -1}]
        config["drafts"] = [{"tab": item["tab"], "item_hex": item["item_hex"], "price": price}]
        priced = scan()
        assert priced["items"][0]["price"] == price
        assert priced["unmatched_drafts"] == 0
        helper = Path(__file__).resolve().parents[1] / "tools/marketplace/cache-offers.py"
        spec = importlib.util.spec_from_file_location("cache_offers", helper)
        cache = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cache)
        catalogue = Path(directory) / "catalogue.json"
        catalogue.write_text(json.dumps(priced), encoding="utf-8")
        now = priced["captured_at"]
        feed = cache.assemble([("Seller A", catalogue)], now)
        assert len(feed["listings"]) == 1 and feed["listings"][0]["price"] == price
        assert feed["expires_at"] <= now + 3600
        unchanged_revision = feed["listings"][0]["revision"]
        priced["items"][0]["price"][0]["quantity"] = 39
        catalogue.write_text(json.dumps(priced), encoding="utf-8")
        assert cache.assemble([("Seller A", catalogue)], now)["listings"][0]["revision"] != unchanged_revision
        for sources, stamp in [([("Seller A", catalogue)], now + 3600), ([("Seller A", catalogue), ("Seller A", catalogue)], now)]:
            try:
                cache.assemble(sources, stamp)
            except ValueError:
                pass
            else:
                raise AssertionError("Accepted expired or duplicate-seller catalogue")
        output = Path(directory) / ("MarketplaceFeed_2.4_" + match[1] + ".json")
        subprocess.run([sys.executable, str(helper), "--seller", "Seller A", str(catalogue), "--output", str(output)], check=True)
        assert json.loads(output.read_text())["listings"][0]["seller"] == "Seller A"
        config["drafts"][0]["item_hex"] = "00"
        stale = scan()
        assert stale["unmatched_drafts"] == 1
        assert all(row["price"] is None for row in stale["items"])
        config["enabled"] = False
        scan(False)
        config["enabled"] = True
        config["page"] = 1 if config["page"] != 1 else 2
        scan(False)
        assert hashlib.sha256(stash.read_bytes()).hexdigest() == original_hash
        print(f"Companion bridge: {len(snapshot['items'])} items; prices, stale drafts, mode/page guards, cache assembly/revisions/expiry and source SHA256 preservation passed.")


if __name__ == "__main__":
    main()
