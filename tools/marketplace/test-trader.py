"""Drive the other side of development trades; never settles or invents prices."""
import argparse
import json
import os
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request
import uuid


class Trader:
    def __init__(self, pairing, endpoint, hardcore=False, allow_local=False):
        url = urllib.parse.urlsplit(endpoint)
        if url.username or url.password or url.query or url.fragment or (url.scheme != "https" and not (allow_local and url.scheme == "http" and url.hostname in {"127.0.0.1", "localhost", "::1"})):
            raise ValueError("Use the paired HTTPS API endpoint")
        if pairing.get("role") != "tester" or len(pairing.get("token", "")) != 64:
            raise ValueError("A dedicated development trader pairing is required")
        self.pairing, self.endpoint, self.hardcore = pairing, endpoint, hardcore
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, *args, **kwargs):
                return None
        self.opener = urllib.request.build_opener(NoRedirect)

    def request(self, action, command=None):
        query = urllib.parse.urlencode({"action": action, "hardcore": int(self.hardcore)})
        body = None if command is None else json.dumps(command).encode("utf-8")
        if body is not None and len(body) > 32768:
            raise ValueError("Command exceeds size limit")
        req = urllib.request.Request(self.endpoint + "?" + query, data=body, headers={"Authorization": "Bearer " + self.pairing["token"], "Content-Type": "application/json"})
        with self.opener.open(req, timeout=30) as r:
            raw = r.read(16 * 1024 * 1024 + 1)
        if len(raw) > 16 * 1024 * 1024:
            raise ValueError("Response exceeds size limit")
        return json.loads(raw)

    def snapshot(self):
        data = self.request("sync")
        if data.get("schema") != 1 or data.get("game_mode") != "2.4" or data.get("mod") != "RMD-MP" or data.get("hardcore") is not self.hardcore or data["self"]["id"] != self.pairing["id"]:
            raise ValueError("Development trader identity or mode does not match")
        return data

    def propose(self, listing_id, price=None, message="Development offer: no inventory or payment will move."):
        listing = next((r for r in self.snapshot()["listings"] if r["id"] == listing_id), None)
        if listing is None or listing["own"]:
            raise ValueError("Choose another player's current listing")
        return self.request("command", {"id": uuid.uuid4().hex, "type": "propose", "listing_id": listing_id, "listing_revision": int(listing["revision"]), "price": listing["price"] if price is None else price, "message": message})

    def respond(self, offer_id, action, price=None, message=""):
        thread = next((r for r in self.snapshot()["offers"] if r["id"] == offer_id), None)
        if thread is None:
            raise ValueError("Conversation is not available to the development trader")
        if action == "counter" and price is None:
            raise ValueError("Choose the exact counter price explicitly with --price-file")
        command = {"id": uuid.uuid4().hex, "type": action, "offer_id": offer_id, "expected_revision": thread["revision"], "message": message}
        if action == "counter":
            command["price"] = price
        return self.request("command", command)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=["list", "offers", "propose", "accept", "reject", "withdraw", "counter", "message"])
    parser.add_argument("id", nargs="?")
    parser.add_argument("--price-file", type=Path)
    parser.add_argument("--message", default="")
    parser.add_argument("--hardcore", action="store_true")
    args = parser.parse_args()
    directory = Path(os.environ["USERPROFILE"]) / ".d2r-marketplace"
    pairing = json.loads((directory / "test-trader-pairing.json").read_text(encoding="utf-8-sig"))
    settings = json.loads((directory / "sync-config.json").read_text(encoding="utf-8-sig"))
    trader = Trader(pairing, settings["endpoint"], args.hardcore)
    try:
        price = json.loads(args.price_file.read_text(encoding="utf-8-sig")) if args.price_file else None
        if args.operation == "list":
            result = [{k: r[k] for k in ("id", "seller", "revision", "price", "development")} for r in trader.snapshot()["listings"] if not r["own"]]
        elif args.operation == "offers":
            result = [{k: r[k] for k in ("id", "status", "revision", "seller_name", "buyer_name", "price", "events")} for r in trader.snapshot()["offers"]]
        elif not args.id:
            raise ValueError("An item listing or offer ID is required")
        elif args.operation == "propose":
            result = trader.propose(args.id, price, args.message or "Development offer: no inventory or payment will move.")
        else:
            result = trader.respond(args.id, args.operation, price, args.message)
        print(json.dumps(result, ensure_ascii=False, indent=2))
    except urllib.error.HTTPError as error:
        try:
            message = json.loads(error.read(4096)).get("error", "Service refused the action")
        except ValueError:
            message = "Service refused the action"
        raise SystemExit(f"HTTP {error.code}: {message}")
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
