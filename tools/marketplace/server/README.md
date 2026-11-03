# DreamHost marketplace service

Live API: **https://market.d2rmodding.com/index.php**. DreamHost hosting, the
certificate, Apache authorization, MySQL and initial PC synchronization are
verified. It needs PHP 8.1+ (64-bit), PDO MySQL and MySQL/InnoDB.
The HTTP API and public website run per request; neither needs a resident server
process. See [website operation](../WEB.md) for browser sessions and shared UI.

## Installation

1. In DreamHost, add `market.d2rmodding.com`, enable HTTPS, and create a MySQL
   database with a dedicated database user. Record the database hostname, database
   name and user. Use the exact MySQL hostname provided by DreamHost.
2. Enable SSH for the website's Unix user. Upload this directory as
   `~/market-service/`. Set the subdomain's web directory to
   `market-service/public`. Only `public/` belongs in the web document root.
   `Market.php`, `bootstrap.php`, `cli.php` and the real configuration must remain
   outside it. Do not upload test databases, save files or player tokens.
3. Copy `config.example.php` to `config.php` beside `bootstrap.php`. Fill in the
   database values locally on the server. Restrict the file to the website user
   (`chmod 600 config.php`). Alternatively set `D2_MARKET_CONFIG` to a private
   absolute configuration path available to both PHP web requests and CLI.
4. From SSH, select a supported PHP CLI executable and run `php cli.php init`.
   This creates the schema and indexes. It is safe to rerun for this schema.
   The web entry point never creates or migrates tables.
5. Run `php cli.php user "Player name" player` for each player. Save the returned
   player ID and one-time pairing token privately. Tokens are stored hashed in
   SQL. Each player uses their own token; do not distribute operator credentials.
6. Pair the Windows companion using `../sync-config.example.json` and the
   instructions below. Verify HTTPS and account identity on a test account first.

The bundled `.htaccess` forwards Bearer authorization to PHP. If requests return
401 despite a valid token, verify that the host forwards Authorization; never
put a token into a URL. Leave `D2_MARKET_LOCAL_TEST` unset in production.

SSH key access and database connectivity have been verified. The database uses
`mysql.d2rmodding.com`, leaving `market.d2rmodding.com` for the HTTPS API. Keep
passwords and private keys out of chat and outside the public document root.
Only `market-service/public` should be configured as the website directory.

## Player sync

Copy `sync-config.example.json` to a private local file, set `player_id` to the
paired player's ID, and make their token available as `D2_MARKET_TOKEN` in that
process environment. Then run, using Python 3.10+:

```powershell
python tools/marketplace/sync-marketplace.py C:/private/market-sync.json "C:/Users/PLAYER/Saved Games/Diablo II Resurrected/mods/RMD-MP"
```

Add `--hardcore` for HC. Refresh the in-game marketplace after syncing. The
command above is one-shot. The installed combined starter manages polling and
a per-user Windows startup/recovery task.
It sends queued offer actions, writes their receipts and refreshes the listing
and activity caches. It never writes `.d2s` or `.d2i` files. HTTPS certificate
validation is enabled and redirects are refused. Pairing tokens are not placed
in sidecar files. Back up SQL and local activity logs before archiving them.

This development computer has a player pairing in the access-restricted
`%USERPROFILE%\.d2r-marketplace` directory. To refresh its marketplace cache, run
`tools\marketplace\sync-dreamhost.ps1` for one-shot SC sync, or add `-Hardcore`.
That wrapper supplies the token only to its child process and restores the
previous process environment afterwards. It does not schedule polling.

## Inventory registration and settlement boundary

The HUD exports priced deposit items to `MarketplacePublish_2.4_SC.json` (or HC)
after a successful live/saved scan, pricing change or setup change. The companion
publishes that complete snapshot as the paired player. Hosted IDs are preserved
for unchanged exact items; price revisions and withdrawals invalidate older
pending/accepted negotiations with visible log entries. A failed scan does not
publish an empty inventory. Disabling the deposit deliberately withdraws it.

`tools\marketplace\start-marketplace-sync.ps1` starts one hidden SC companion
on this PC, polling every 15 seconds; `stop-marketplace-sync.ps1` stops that
specific worker. Add `-Hardcore` for HC. The combined starter installs a Windows
startup/recovery task when passed `-InstallStartup`. In-game Refresh reads the updated caches. No network work runs in the DLL.

Discovery snapshots are **unsecured**: items remain on their owner's PC. They
expire 24 hours after capture; retrying the same snapshot does not extend it.
Refresh the inventory to produce a current capture. Publication cannot authorize
an item debit, payment, delivery, escrow ownership or completed sale. The server
validates structure but does not authenticate client-supplied saved bytes.

The operator import path remains available for trusted inventory registration.
`php cli.php import-listing FILE SC` accepts a private JSON document containing
`owner` (player ID), `price` (the exact bundle), and `item` with `seed`, `code`,
`quality`, `index`, `ethereal`, `sockets` and serialized `item_hex`.
Metadata must be produced by a trusted parser using matching RMD-MP tables.
The service validates structure; it does not parse or authenticate saved bytes.
Use `item_id` instead of `item` when relisting an existing ledger instance.

Accepting an unsecured or unfunded offer records an agreement. For funded
escrow listings, acceptance atomically transfers ownership and every payment
asset/balance, marks the offer completed and records a durable sale receipt.
Delivery to a physical stash is a separate idempotent claim. Automatic approval
requires exact asking terms, the seller's saved preference and complete escrow
funds. Testers cannot settle. An operator's `record-sale` import remains available
only for inventory outside active escrow; it cannot override custody ownership.

The separate save-agent API uses `POST index.php?action=custody`. Pair with
`php cli.php pair-agent PLAYER_ID CATALOG_SHA256`; keep its token separate from
the player's token. Supported operations are `deposit_prepare`, `claim_prepare`,
`write_authorize`, `commit`, `status`, and pre-write `cancel`. IDs are stable
32-character lowercase hex. Every call pins `catalog`. Deposit prepare includes
exact items/resources and per-page before/after hashes; write authorization must
match before the agent commits removal. Claim prepare returns server-owned
parcels and supports `item_limit` and typed `balance_caps`; placement authorization
includes each ledger item's delivered bytes. A writing claim cannot cancel.
See [the save protocol and recovery](../CUSTODY.md#live-protocol-and-recovery).

Player commands also include `preferences` (`auto_trade`), `escrow_price`
(`item_id`, `price`), and `escrow_return` (`item_id`). Repricing/returning an item
invalidates its pending or accepted offers. Sync includes private `escrow` items,
resource balances and enrollment state. Delayed discovery cannot recreate a
listing for an item that already left its local source page through custody.

Seeds are searchable within community/mod/version/SC-or-HC. They are not globally
unique: separate ledger IDs distinguish collisions and cloned seeds. Relisting
the same ledger ID preserves resale history. The native fallback refuses to
merge ambiguous seeds. Similar sales match base code, quality, named-item index,
ethereal state and socket count; individual rolls may differ.

## API and visibility

Every request requires `Authorization: Bearer TOKEN` over HTTPS. Add
`hardcore=0` or `hardcore=1` to every request.

- `GET index.php?action=sync`: account identity, open listings, completed sales,
  only the authenticated player's conversations, and their publication revision.
- `POST index.php?action=publish`: full native publication envelope, snapshot
  `id`, `captured_at`, `expected_revision`, page, enabled/source and priced item
  rows. Maximum 512 priced items / 8 MiB. Owner comes from authentication, never
  the payload. Retries with the same ID/content are idempotent; stale revisions,
  duplicate rows, changed metadata for the same bytes, and escrow claims fail.
- `POST index.php?action=command`: JSON command with unique `id`; types are
  `propose`, `counter`, `message`, `accept`, `reject`, `withdraw`. Proposals require
  `listing_id`, `listing_revision`, `price`. Replies require `offer_id` and
  `expected_revision`. Messages use `message`. Counters also require `price`.
- `GET index.php?action=history&item_id=ID`: completed-sale history for that
  ledger instance. Alternatively use `code` and `seed`; such results may contain
  multiple ledger instances and must not be silently merged.
- Similar history uses `code`, `similar=1`, `quality`, `item_index`, `ethereal`,
  `sockets`. History pages return up to 200 records, `complete`, `next_before`.
  Continue with `before=next_before` to retrieve older records.

Exact barter terms are arrays of 1–16 AND requirements. Requirements support
item identities, gold, and approved RMD-MP bag-stat balances. There is no implied
currency conversion. Conversations are private to their two parties; completed
sales and listing discovery are public through the website. Native sync still
requires a player token. Browser users connect an operator-paired player once,
using a save-folder link or a short Pandemonium code. No website credentials
are created, and arbitrary inventory identities cannot self-register. See [browser API and sessions](../WEB.md).

## Development trader and in-game test

A dedicated `tester` account named **Development Trader** is paired privately on
this PC. Its token is separate from the player's. Use `test-trader.py` to propose,
message, counter and decide on player listings. The local third-page simulator
and its in-game controls are retired; old settings are disabled while preserving
prices and logs. The legacy test-publication sidecar remains understood so a
disabled publication can withdraw old test listings. The server refuses
completed-sale records involving a tester account.

1. Start the companion, open deposit page 11, and open Marketplace > My Items.
2. Set/re-save one exact asking price and Refresh. After about 20 seconds,
   Refresh again. Browse should show **Your listing / Hosted asking price**.
3. The development driver can offer that exact manually chosen price. Refresh
   Offers & Logs to read it, send a message, counter, reject or accept.
4. Counter prices are chosen explicitly. Development Trader acceptance changes the negotiation
   state only. Repricing/removing the listing supersedes older negotiations.

Driver commands (developer tooling, not another player menu):

```text
python tools/marketplace/test-trader.py list
python tools/marketplace/test-trader.py propose LISTING_ID
python tools/marketplace/test-trader.py offers
python tools/marketplace/test-trader.py accept OFFER_ID --message "Development response"
python tools/marketplace/test-trader.py counter OFFER_ID --price-file PRIVATE_PRICE_JSON
```

The driver uses the current listing's asking terms when proposing unless an
explicit price file is supplied. It never generates seller prices or completes
sales. Pairing files and worker logs stay in the restricted local application
data directory; no token appears in game sidecars or command-line arguments.

## Current limits and verification

SQL retains all events. Sync snapshots include the newest 1,000 completed sales,
256 conversations and 4,096 open listings. The native history view reads this
snapshot; fetching older API history pages from the game is not connected yet.
Graphs show up to 16 matching single-currency sales, with time on the horizontal
axis. Mixed barter bundles remain readable as exact terms and are not charted.
Conversations stop accepting new messages/counters at 500 events, leaving room
for a final decision. Native full-message input supports up to 512 characters
(2,048 UTF-8 bytes). Local outboxes are bounded to 4,096 commands.

Tests exercise real PHP/PDO SQLite and the local PHP HTTP API with the Python
companion: auth/privacy, exact prices, stale revisions, retries, acceptance vs
completion, resale lineage, seed collisions, pagination and untouched save
sentinels. The same domain tests also passed on DreamHost's MySQL 8.0 using
connection-scoped temporary tables; no test listings or sales were retained.
Public HTTPS, Apache authorization forwarding, authenticated SC/HC requests,
history pagination requests, HTTP-to-HTTPS redirection and the PC sync wrapper
are also verified. Initial sync left all 31 existing game saves byte-for-byte
unchanged. UI fixtures and simulated completed sales are confined to disposable test
databases. The real in-game escrow deposit and unsold return were verified
against the live service without recording a fictitious sale.

```text
php -d extension=pdo_sqlite tests/marketplace-server-test.php
php -d extension=pdo_sqlite tests/marketplace-publication-test.php
python tests/marketplace-sync-test.py PATH_TO_PHP
```

For the portable Windows runtime, also set `extension_dir` to its `ext` folder.
The tests create temporary accounts/databases and never contact DreamHost.
Publication and domain tests additionally passed against connection-scoped
temporary tables on DreamHost before the additive schema update was activated.


### Owner controls and trade statistics

Grant the community owner by stable player ID with `php cli.php owner PLAYER_ID`.
This capability is independent of moderator status and display names, and cannot
be transferred through the website. Owner tools provide three private demo sizes:
24 / 64 / 100 fake players, 100 / 320 / 600 open listings, and 80 / 240 / 480
completed sales. The demo uses a separate SQLite database in `operator-data`,
outside the public directory. It reuses the website's actual trading logic with
synthetic assets; bearer/custody routes always use the live database. Demo actions
cannot alter game saves. Its fake traders reply to messages and negotiate offers.

The live reset first pauses trading and returns all real escrow holdings through
the normal companion delivery journal. Final purge requires no held items,
balances, queued withdrawals or unfinished save operations, the current reset ID,
and `CLEAR MARKETPLACE`. A private compressed recovery archive is saved first.
Real player accounts, names, links, mod registrations and permissions remain.
Technical command/custody receipts remain for exactly-once retry safety. Each
paired client must clear old drafts and outboxes before publishing again; this
requires the accompanying HUD and Python companion update. Reset never directly
edits game saves. Resuming trading during a drain leaves already queued returns
in place. Demo deletion has its own generation check and `CLEAR DEMO` confirmation.

The public Trade stats page is scoped to the selected mod, version and SC/HC mode.
`GET web.php?action=stats&window=7|30|all` returns settled trade counts, ten recent
sales with original prices, eight popular item identities, and eight sellers
ranked by completed sales. Open listing counts describe the current market.
Agreements and pending offers do not count; unlike barter currencies are not
summed. The owner demo's completed settlements feed the same endpoint and UI.


Demo regeneration now gives 20–24 featured live listings two to four prior
settlements each, with continuous seller/buyer ownership on the same ledger ID.
Those settlements are included within the preset's total sale count. Paid
featured listings receive two to four competing open offers; free listings keep
their immediate-claim behavior. The rest of the market retains varied activity.

`history&averages=1` also returns averages over completed matching sales from the
last 30 days. Each payment bundle is grouped by its exact currency/item criteria,
independent of line order. Mixed barter stays a bundle; free transfers have a
separate count. The largest four groups are returned with their sample sizes.
At most the latest 5,000 matching sales are sampled, with an explicit truncated
flag and UI label. Pagination never changes the averaging sample. The website's
item pane shows this guidance for similar items and keeps exact-item ownership
history and the anonymous current-offer board in their existing tabs.


The shared website inspector shows actual saved socket children (runes, gems,
and jewels), empty capacity from saved stat 194, and jewel properties on demand.
Native-style socket holes overlay the item image on hover or focus. This applies
to trade cards and player/mercenary equipment through the same renderer. Child
name IDs resolve from their saved prefix at sync time; serialized child bytes
are stripped. Previously stored metadata without names refreshes on the next
companion upload. Hidden stats and unidentified jewel modifiers remain masked.
The private demo includes a socketed Grandfather for visual inspection.


### Search, activity and bundle tools

Advanced browser controls live under **Refine search**. Visible saved modifier
ranges (including identified jewels), sockets and ethereal state refine the
items offered. Text can still target offered items, wanted payment or both.
A price cap applies to one Gold or Zod payment line; mixed barter is never
implicitly converted. Up to 12 scoped searches can be saved, with individual
alert toggles. Website/companion inbox polls evaluate new listing revisions;
existing matches are baselined, notices are batched and repeated polls dedupe.
Gold/rune sale means now include medians, ranges and sample sizes. Bundle prices
are explicitly labeled in history and excluded from individual-item averages
and charts. Comparison uses public saved character/mercenary snapshots and
saved item modifiers, with conditional/runtime limitations stated in the view.

**Activity** shows new sales/purchases, outstanding offers, current delivery
requests, wallet withdrawals and agreed trades waiting for escrow. Current
queues are read separately from historical receipts; relisted/sold-again items
do not appear as undelivered purchases. A paired-agent heartbeat distinguishes
an offline companion from a waiting page handoff. `activity_read` advances an
owner/scope cursor. Game message preferences and mute controls suppress native
notifications while retaining the website archive. Blocking prevents private
messages in both directions and preserves existing trade settlement/records.
Updated companions opt into filtered inboxes, honor the server cursor across
suppressed batches, refresh preferences even with a full offline buffer and
retain the 256-message bound. Older companions keep their existing inbox feed.

Bundles contain 2–16 existing escrow items with one listing/payment/settlement.
Every member is validated before any debit; all ownership changes share one
transaction. Individual ledgers retain labeled bundle history, while aggregate
sales count the trade once. A still-open bundle can be dissolved, invalidating
its pending bids and keeping its members in escrow. Individual price/return/gift
commands refuse bundled members. All members must finish delivery before a
bundle-delivered notification is emitted. Browser views show every member at
review time; older native menus hide bundle listings/offers rather than display
incomplete terms. Delivery continues through the existing game handoff.

The private demo generator includes two real-domain bundle examples and two
saved-search examples without changing its advertised listing/sale totals.
`tests/run-marketplace-resilience.ps1 -ModData ... -ReferenceSaves ...` runs the
repeatable isolated settlement/identity/wallet/lease and real-copy HTTP/save
interruption matrix. It never writes to the supplied reference saves and stores
its latest report in `build-marketplace/resilience-last.json`. Additional MySQL
validation includes concurrent free-bundle claimants with one winner and exact
member/payment conservation; every validation table uses a disposable prefix.
