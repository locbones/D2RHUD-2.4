# Guarded custody and recovery — D2R 2.4 / RMD-MP

There are two distinct tools:

- `custody-workspace.py` remains a disposable, copy-only recovery rehearsal.
- `custody-agent.py` is the enrolled Windows save agent. It connects guarded
  removals and deliveries to separately authenticated DreamHost custody tickets.

The live agent requires the updated Pandemonium and HUD gameplay lease. It never edits character saves. While a game runs, it can edit only trade pages explicitly unloaded by the native stash handshake. See the
[live protocol](#live-protocol-and-recovery) below before operating it.

After replacing the launcher or HUD, close both applications and revalidate the installed files against the exact tested build artifacts with `revalidate-custody.ps1 -VerifiedHudPath <built-d2rhud.dll> -VerifiedLauncherPath <published-Pandemonium.exe>`. Omit the launcher argument for a HUD-only update. Revalidation backs up the enrollment and changes only the matching binary pins; helper/catalog drift, a pending transfer, or a mismatched artifact is refused. It never edits a save or starts a cloud transfer. Restart the companion after updating its installed scripts. A blocked companion now publishes its verification error to the native activity cache instead of leaving delivery apparently pending with no explanation.

The native marketplace reloads saved price settings when reopened or refreshed, including identities repaired by the companion. The website checks for changes every five seconds while visible, with overlapping requests and modal edits excluded.

`CustodyPlan.h` and `custody-save.exe` parse the same current-format seven-tab
pages as the HUD. They prepare complete replacement page images in memory:

- Select an item by exact tab, ordinal and serialized bytes. Seeds alone never
  authorize a removal. Socket children stay with the parent.
- Match exact item/set/unique, ethereal and identified payment requirements;
  priced items, including a listed bag's contents, cannot fund another trade.
- Split supported normal item stacks without changing the other saved bits.
- Debit gold across the seven tab headers. Gold credits require an explicit
  per-tab capacity supplied to the rehearsal, not an inferred game limit.
- Debit/credit supported existing normal Storage Bag stats using symbolic names
  bound to the exact compiled ItemStatCost BIN. Missing, duplicate, unsupported,
  negative or overflowing balances fail. The tool never synthesizes a bag or
  adds an absent saved stat.
- Place item deliveries in free cells using compiled dimensions and RMD-MP's
  16-by-13 shared grid. A full page or insufficient resource capacity rejects
  the complete plan before any copied page changes.
- Reparse every output. Unchanged pages round-trip byte for byte. Item properties,
  saved identity and socket payloads survive moving to a different grid cell.

## Workspace journal

`custody-workspace.py create SOURCE NEW_DIRECTORY` copies numbered stash pages
and marketplace preferences into a new isolated directory. It reads the source
twice to detect an in-progress save. Incomplete pages can be copied for diagnosis
but cannot produce a custody plan. Character `.d2s` files are never copied or
written. The resulting workspace contains serialized items; keep it private.

Only marked workspaces outside their source save folder are writable, and a
workspace inside a `Diablo II Resurrected` directory is explicitly refused.
Debits must target the enabled deposit; credits must target the delivery page.
SC and HC preferences/routes are separate.

Prepare validates all requested pages before publishing a transaction directory.
The directory retains immutable before/after images, SHA-256 hashes, removed
parcels, the exact request hash, and `settled: false`. It also pins the planner
binary and all five compiled item/stat/type tables plus their metadata. Changed
tables or planner binaries block application/recovery until reconciled.

One process lease serializes workspace operations. A stable transaction ID is
idempotent; changing its request is refused. A pending/conflicted transaction
blocks preparation of a new one. Applied transactions form a checked chain of
page hashes, so retrying an older transaction cannot debit it again.

Application durably records `applying` before replacing a page. On Windows,
replacement uses `MoveFileExW` with `WRITE_THROUGH`, after flushing each image.
There is no multi-file atomic rename: an interrupted transaction can temporarily
contain a mixture of before/after pages. Recovery recognizes that exact mixture
and finishes the prepared images once. An unrelated edit, corrupt image, or
restored old page causes a conflict; it is never silently overwritten or used
to repeat the removal. Interrupted preparation can safely resume before any
transaction is published. Backups and journals are retained.

## Build and reproduce

From the repository in an x64 Visual Studio developer prompt:

```text
tools\marketplace\build-custody.cmd build-marketplace\tools
tests\run-marketplace-custody.cmd build-marketplace\custody-tests MOD_DATA SAVE_FOLDER
python tests/marketplace-custody-workspace-test.py build-marketplace/tools/custody-save.exe MOD_DATA SAVE_FOLDER build-marketplace/custody-rehearsals
```

The Python suite builds an isolated fixture from real serialized items, with
controlled test gold and an empty copied delivery page. It exercises exact
conservation, multi-page preflight, ID retries, writer exclusion, conflicting
restores, corrupt backups, changed tables, and four abrupt child-process exits:
during image preparation, after `applying`, and after each page replacement.
Every original `.d2i` and `.d2s` hash is checked afterwards. No network request or
production sale is made.

For manual inspection:

```text
custody-save.exe inventory MOD_DATA STASH_PAGE
python tools/marketplace/custody-workspace.py create SAVE_FOLDER NEW_WORKSPACE
python tools/marketplace/custody-workspace.py prepare NEW_WORKSPACE custody-save.exe MOD_DATA REQUEST_JSON
python tools/marketplace/custody-workspace.py apply NEW_WORKSPACE TRANSACTION_ID
python tools/marketplace/custody-workspace.py recover NEW_WORKSPACE TRANSACTION_ID
```

A request has `schema: 1`, a unique 32-character lowercase hex `id`, a boolean
`hardcore`, and one or two `edits`. Each edit specifies `page` and optional
`take` selectors (`tab`, `ordinal`, `item_hex`), `debit` exact payment lines,
`credit_items` with serialized `item_hex`, `credit_balances`, and `gold_cap`.
Credits belong to the delivery route. A rehearsal may explicitly supply its
test parcel; it is not evidence of remotely owned items. Do not expose this
interface as a player-authorized delivery endpoint.

## Live protocol and recovery

Enrollment pins the helper SHA-256 and a canonical fingerprint of the five
compiled item/stat/type BINs plus the hash-bound name metadata. The service pins
that catalog per community/mod/version/SC-or-HC scope. Player and save-agent
credentials are separate. Only the paired agent can prepare/authorize/commit
custody; a player's UI token cannot mint inventory or delivery parcels.

Each operation durably writes immutable before/after images, checksums and a
pending pointer before its first network call. Deposit prepare stores the exact
parcel but makes nothing spendable. Write authorization pins save fingerprints;
the agent writes/reparses the complete page, verifies its SHA-256, and commits the
server ticket. Only then are goods and resource balances activated in escrow.
Interrupted or unanswered requests reuse the same ID/content. A checksum error,
changed catalog or unrelated save edit pauses trading for reconciliation. It
never blocks game startup, and the changed save is never overwritten.

Claims reserve available assets and remove reserved resources from spendable
balances. The agent asks only for delivery resource capacity it has verified,
plans native grid placement, then authorizes the exact placed bytes and save
images. The service permits changes only to root stash location bits. Items,
properties and socket children are preserved. A capacity failure before write
authorization releases the reservation; an authorized operation must recover,
never cancel blindly. Lost acknowledgements cannot repeat delivery. The ledger
records delivered bytes so an exact returning item can retain its history even
with a zero seed. Collisions/ambiguous serialized identities are not merged.

A scope transaction lock serializes offers, listing changes, deposits, claims
and settlement. Settlement checks exact available payments before changing
anything, then transfers goods, every payment asset/balance, listing/offer state,
and one durable sale receipt in a single InnoDB transaction. Partial normal
stacks retain their total quantity and distinct ledger instances. Pending claims
and listed assets are excluded. Automatic approval requires the seller's enabled
preference, a buyer-originated proposal matching every asking term, and complete
escrow funds. Tester accounts cannot settle. An old unsecured agreement is
superseded when its goods first enter custody and must be reviewed again.

Marketplace availability never gates game startup. Pandemonium does not run a
network/Python preflight. It holds a shared `game.lock` lease across process
creation until the HUD takes over until process exit. Enrollment errors, changed
binary pins, missing Python, pending journals and service outages pause trading;
they do not reject launch or HUD initialization. The independent gate does not
require parsing enrollment or inspecting transfer journals.

The companion's `guard.lock` serializes cloud transactions. The separate
`game.lock` is held exclusively only around local save/sidecar replacement,
never around network requests or helper processes. Immediately before writing,
the agent also checks a local Windows process snapshot and rechecks every page
against its authorized before/after images. A game starting during a cloud
request defers the journal; any later unrelated save edit requires marketplace
reconciliation instead of overwriting the player's changes.

Explicit item returns, purchased items and physical item proceeds are persistent service-side requests. Every eligible
companion pass receives the outstanding item IDs and claims only those items,
without sweeping unrelated payment stock, gold or bag balances from the wallet.
The request survives an empty launch check, capacity failure, process restart or
late native/browser command. It is cleared only when the exact delivery save
write is acknowledged. A full delivery page keeps the request queued for retry.

With the live bridge, the HUD publishes a schema-2 `unloaded` handoff identifying
the one currently loaded page. Viewing deposit page 11 permits delivery to page
2; viewing page 2 permits deposit processing on page 11. The loaded page is
excluded from every save read/plan/write permission. A page switch first
reacquires the shared gate and revokes the handoff; it remains revoked through
the save, unload, load and final server acknowledgement. Only after that ack
does the HUD publish a fresh handoff and release `live.lock`.
The companion validates the process ID and creation time, game/save paths,
owner, catalog, mode, routes and heartbeat under an exclusive `live.lock` before
publishing a journal or replacing trade pages. Multiple running D2R processes
disable live writes. It never writes the loaded page. A schema-1 parked handoff
from an older HUD still requires both trade pages to be unloaded.

Reopening the stash reacquires the shared gate before loading a trade page.
Protected pages are packetized from a fresh, stable disk read rather than the
game's cached save-file blob. This uses the native seven-tab save-header protocol,
including its ignored header-zero slot, without reading or re-sending a character
save. Every tab and fragment is validated before the page switch starts.
Pending journals hold only the deposit/delivery pages; other pages remain usable.
The original page restores after a sync result or a bounded wait when no journal
exists. Native callbacks never wait for network work. Viewing another page while
the companion syncs is enough for ordinary deposits and requested deliveries.
Updated contents appear when that page is next selected. Closing the stash can
still park its inventory on an ordinary page, making both routes available. Keep a
normal bag and free cells in the delivery page. The conservative gold ceiling is
2,500,000 per shared tab. No network work or cleanup thread runs during game
shutdown.

Closed-game access remains the fallback when the native bridge is unavailable.
Pandemonium also queues a wallet collection pass for when the game closes.
Ordinary live passes claim only requested items, preserving unspent wallet gold
and bag balances for other purchases. Transactions retain the same immutable
images, fingerprint checks and exactly-once recovery in either access mode.

Clicking a priced deposit item first opens a native withdrawal prompt. Keeping
the listing leaves the original action blocked and its price unchanged.
Withdrawing clears the asking price and linked local actions, publishes the
withdrawal and queues an idempotent service command to cancel pending offers
and queued website prices. The player can then move the item normally. The
prompt never replays a click or changes an inventory. Completed sale receipts
remain in history. Local outbox/publication changes wake sync immediately;
HUD heartbeat renewals do not trigger additional HTTP polling.

For a development HUD update, after verifying its gameplay-lease integration,
install that build with the game closed and run:

```powershell
.\tools\marketplace\revalidate-custody.ps1 -VerifiedHudPath .\x64\Release\d2rhud.dll
```

This checks the installed HUD against the supplied build, validates the existing
launcher/helper/catalog pins using the installed agent, and atomically updates
only the HUD pin with a backup. It never edits a save or contacts the service.
Changed launcher/helper/catalog versions require a verified enrollment update.
A stale HUD pin affects trading only; gameplay remains available.

TCP/IP client-owned saves and manually restored backups remain a trust boundary.
The journal rejects transaction replay and conflicting save images, but cannot prevent a player
from deliberately editing saves or bypassing a modified client. DreamHost stores
escrow rather than trying to edit an unreachable sleeping PC.

Persistent item identity is decoded from serialized version-98 bytes on both
client and server. The optional 16-byte saved UUID takes precedence; otherwise
extended items use their legacy seed and base code. Zero-seed extended items
fall back to a position-independent byte fingerprint. Compact items without
an identity are fungible: the service never treats every identical rune as one
unique object. Every escrow instance also has a separate durable history ledger.

Admission checks that identity against server ownership and reserves it before
any local removal is authorized. Moving a restored item to another tab, slot,
page or account does not permit re-hosting an identity already held or sold.
Only its current owner can re-deposit a delivered item with the exact existing
ledger and bytes. Collisions are refused for reconciliation, never silently
merged. Normal payment-stack splits receive a new saved UUID or legacy seed,
matching native stack splitting, while preserving modifiers and total units.
Rejected prepares are journaled and cleared only after a server 404 confirms no
ticket exists and all local pages still match their original images.

These checks detect replay of known saved identities, not arbitrary save editing
or newly reseeded counterfeits. The optional UUID is absent from many legacy
saves; a 32-bit seed is not globally unique. Identity-less currency and restored
bag/gold balances cannot be authenticated by an item UUID. Enforcing those
against a hostile save owner ultimately requires an authoritative game server.
Whole-page hashes are retained as recovery/audit evidence, not a global veto on
future deposits: two legitimate 100-gold deposits can produce identical bytes.
Persistent item admission still checks saved identity, current ownership and
delivered lineage before any removal. A failed deposit with no pending journal
does not block independent queued deliveries.

The client-side native serializer may omit persisted seeds for an item and its
socket children. Live views reconcile only a unique, otherwise identical saved
counterpart, preserving current position. A new item without a verified saved
identity waits until the native save/page handoff or game close. The save companion repairs old zero-seed
asking keys only when their complete normalized item/socket bytes match one
saved item, then publishes the canonical save bytes. Ambiguous matches stay
unlinked. Unverified extended-item discovery is excluded from public feeds and
cannot receive new offers; existing historical records are preserved.

## Explicit wallet withdrawals

`wallet_withdraw` reserves an exact gold/bag bundle from existing available
balances; `wallet_cancel` uses the request revision and releases only its
undelivered remainder. Owner/mode scope and idempotency are checked in the same
transaction as the balance change. Reserved funds cannot satisfy trade payments.
`web_sync.wallet_withdrawals` advertises queued requests to the paired agent.
The authenticated `delivery_queue` poll also returns item and wallet requests
without publishing inventory or applying settings. This allows delivery while
the deposit page is loaded, preserving its existing private inventory snapshot.
A `claim_prepare` with `withdrawal_id` reserves only resources fitting the
reported delivery capacity and includes no unrelated items. A cancelled,
unauthorized save plan returns the claim to the request; a committed plan
reduces its remainder exactly once. Interrupted authorized writes retain their
original journal/proofs and recover before further work. A capacity failure
retains the request and reports delivery guidance through `wallet_notice`.
Ordinary launch/background passes deliver requested items and explicit wallet
requests; they retain other unspent balances and unrequested payment stock.
An open native wallet continues reading the local cache after its command is
acknowledged. Completed receipt journal changes wake the cache sync immediately;
idle status/heartbeat writes do not trigger extra HTTP polling. Each withdrawal
keeps one row whose state changes from queued/delivering to completed, with its
cancel action removed. Finished requests remain available as history.
