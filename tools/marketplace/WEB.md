# Website marketplace

The public exchange lives at **https://market.d2rmodding.com/** on the existing
DreamHost PHP/MySQL service. The website and native game use the same actors,
listings, escrow, exact barter prices, negotiations, messages and sale ledger.

## Player linking

Anyone can browse without linking. To manage inventory and trades, choose
**Link player** and enter a code from
**Pandemonium → Settings → Marketplace → Get link code**. Code linking is the
only browser-linking method; the website does not ask players to select files
or save folders. There is no website username, password, registration form, or
password-login endpoint.

Codes are short-lived, single-use credentials for the existing paired player,
not permanent player or save-agent tokens. Pandemonium can generate a code and
open the linked website directly. The existing desktop shortcut also uses a
one-time code. Codes look like `ABCD-EFGH-JKLM`, expire after five minutes, and
are stored hashed on the service. Multiple outstanding codes do not invalidate
each other.

The browser remembers the linked player through a Secure, HttpOnly,
SameSite=Strict cookie with a maximum 90-day lifetime. Every POST, including
linking, requires a CSRF token. Linking rotates the session ID; permanent player
pairing rotation invalidates sessions. Linking attempts are rate limited.
**Unlink this browser** removes browser access without changing the game's
pairing, listings or other devices. Session files stay outside the public root.
An operator can issue a one-use code with `php cli.php web-code PLAYER_ID`.

## Shared functionality

- **Browse:** item cards, names/artwork from the wiki, saved item properties,
  quality/category/search/seller sorting, escrow-ready and affordability filters.
- **My items:** escrow goods plus the latest private deposit-page snapshot;
  searchable prices combining normal/rune items, specific unique/set identities,
  gold and supported bag balances. Review exact AND terms before saving.
- **Offers & logs:** asking-price offers, counter-proposals, accept/decline/
  withdraw, messages, complete event logs and current exact terms. State changes
  use the same revision checks and atomic settlement as the game.
- **History:** item ledger/seed searches, comparable item sales, older-history
  pagination, and graphs for a selected single payment identity. Mixed barter
  bundles remain exact readable records without an invented exchange rate.
- **Setup:** deposit/delivery pages, deposit enable, automatic exact-price
  approval, companion status, queued-change receipts and linked-player management.
- **Escrow wallet:** available gold and bag balances. Unlisted goods/proceeds
  are delivered by the companion when trade pages unload on stash close, or while the game is closed; physical capacity remains authoritative.

Escrow actions take effect on the service immediately. Website changes to local
stash prices/routes are queued as `web_intents`, scoped to the authenticated
player and SC/HC mode. The paired save agent publishes a private snapshot and
applies those settings only under guarded closed-game or native parked-page access.
It durably writes the sidecar before acknowledging an intent. An exact item
that moved/changed is rejected with a visible receipt instead of repricing a
different item. The existing staged deposit protocol then moves priced goods
to escrow. Nothing in a browser session authorizes a raw save write or mints
inventory. The website cannot inspect unsaved game memory; the snapshot's last
checked time is shown. Older consumed inventory cannot reappear as a sale.

## Design and development

The site reuses the community wiki's Sanctuary palette, forge artwork/sigil,
Cinzel headings, Inter body text and existing item PNGs. Font licenses are
included in `server/public/assets`. Public responses omit serialized item bytes
and all native tokens. Saved values are used for properties; static unique/set
definition ranges are never presented as the listed item's actual rolls.

Rebuild the web catalog/art bundle from the wiki source:

```text
node tools/marketplace/build-web-catalog.cjs PATH_TO_RMD_WIKI
```

Run the isolated UI preview (PHP with PDO SQLite required):

```text
python tools/marketplace/preview-web.py --php PATH_TO_PHP --directory NEW_TEMP_DIRECTORY
```

This preview seeds a disposable local database; it never contacts production.
The production exchange is not populated with UI samples or fictitious sales.
The generated preview account/code remains outside the public directory.

```text
php -d extension=pdo_sqlite tests/marketplace-web-test.php
python tests/marketplace-web-http-test.py PATH_TO_PHP
```

Deployment includes `Browser.php`, the normal service sources, and `public/`
HTML/CSS/JS/artwork plus `web.php`. Run `php cli.php init` before activating the
new code; migrations are additive and never run during web requests. Preserve
`config.php`, existing SQL, private pairings, and sessions. Production checks use
connection-local temporary tables to avoid inserting test inventory or sales.
# Collection display

Tiles keep seller, status and favorites in a single compact toolbar below the
price. The actions menu contains the full status, seller controls and offer
link. Item names open trades; item images retain the property tooltip.

List view uses fixed-height rows with an image, item name and inline asking
price. Clicking the price opens the complete barter bundle without increasing
row height. Lines selects 10, 20, 40 or 80 results per page in Browse and My
Items. Tile size and list line count are remembered independently in the browser.

Item titles resolve saved magic-affix and rare-name IDs using the mod's localized
name tables. Unique/set titles and artwork retain their identity on upgraded
bases. Browser snapshots derive these IDs from existing serialized items at
read time, so old listings and private inventory do not require relisting.
Unidentified items keep a generic title. Serialized custody bytes remain private.

Wallet withdrawals are explicit in both the website and native HUD. Choose a
resource and amount, review it, then queue it. The server removes the requested
amount from spendable funds immediately. A guarded stash sync delivers as much
as fits; an incompatible/full delivery bag or full gold storage leaves the
remainder reserved with an actionable message. A queued remainder can be
cancelled back to the trading balance. Delivery in progress cannot be cancelled.
The wallet lists requests and recent deposits, trade payments/proceeds and
reservation/cancellation activity. Existing balances survive the upgrade.
The updated companion does not automatically sweep wallet funds at game launch.

## Public players and private messages

The **Players** tab lists players who have linked a browser with a Pandemonium
code. Linking enables public character previews by default. The paired companion
publishes equipped gear, carried inventory, belt and mercenary equipment from
valid, last-saved character files about once a minute. Personal/shared stash,
cube contents, corpses and golem items are excluded. No character save file or
local path is uploaded. Previews are display-only and never grant trading custody.
Each character shows its last-saved time; character and mercenary gear use the
same item artwork and property tooltips as marketplace listings.

Open a player and choose **Message**. Private conversations and trade updates
appear in the **Messages** tab. Only their participants can read them. Linking is
required to send or read messages; guests can browse public character previews.

While RMD-MP is running, the native Game chat receives website messages labeled
`[Message]` and offer updates labeled `[Trade]`. Use `/mt "Player Name" message`
to send a direct message, `/mr message` to reply to the last incoming direct or
trade message, and `/market` for help. Trade replies append text to the existing
private negotiation; they never accept an offer or change its terms. These
commands are intercepted before entering TCP/IP game chat. The existing Game
channel is used; this release does not add an unverified native channel type.
The companion polls about every five seconds during an active chat session and
keeps a bounded, durable mailbox with a delivery cursor, so reconnecting does
not repeat messages. The game sends normal chat through its original path.


### Native-style item inspection

Character and trade images share `market-tooltip.js`. Deploy it with `market.js`,
`market.html`, `market-items.css`, and the regenerated `assets/catalog.json` and
`assets/exocet-medium.otf`. The catalog generator takes skill names, creature
names, stat groups, and weapon type/speed descriptions from the RMD game tables.
It recognizes the mod's `aura_display` and `oskill_display` names and custom proc
stats by their description functions. Raw skill IDs are never player-facing
fallback labels. The tooltip keeps saved seed/UUID/history metadata under
**Item identity**, available by tapping the image to pin the tooltip.

Plain weapon damage includes saved local physical modifiers. Where sockets or
character-level damage require more context, the viewer explicitly labels table
values **Base damage**. It does not claim those values are the equipped character's
final combat damage. Rolled defense/durability are not inferred from base ranges.

Every character profile has a messaging entry point: **Message player** for other
players and **Message yourself** for the linked player's own profile. Guests are
prompted for their Pandemonium link code before private messages are opened.

Validation: `node tests/marketplace-tooltip-test.js`, the item naming/collection/
search regression tests, and the browser HTTP session/settlement suite.

Hidden properties follow the mod's tooltip visibility: definitions containing
`hidden`, or lacking a description function/string, never render as property
lines. Gameplay-only `item_aura`/`item_nonclassskill` stats stay invisible; their
separate `aura_display`/`oskill_display` stats render the intended named effects.
The catalog uses compiled row positions for stat IDs, never the informational
`*ID` comment column (which contains duplicate IDs in the mod).

Self-messaging is available from your profile and the Messages toolbar via
**Message yourself**. It uses the same private conversation, rate limit,
idempotency, companion inbox, and native chat path as any other direct message.
The self-conversation is labeled as a test and is visible only to its owner.


### Trade notifications

Offers, counter-offers, trade messages, acceptance, decline, withdrawal, closed
or superseded offers, completed escrow trades, and acknowledged purchased-item
delivery use the same durable private inbox as direct messages. Notifications
include the item name, an explicit event label, and the applicable barter terms.
Repricing or removing a listing closes affected offers and alerts their bidders.
A completed trade confirms both seller and buyer; the winning offer does not
receive a contradictory cancellation. Delivery is announced only after the
recipient's guarded save-write commit, not when a delivery is merely queued.

The companion forwards these entries to the existing native `[Trade]` chat
adapter. Website **Messages → Trade updates** shows the same entries. `/mr`
replies to the attached private negotiation, including after completion; it
never changes terms or accepts an offer. Unrelated players cannot read these
messages. Command/custody retries reuse their event receipt, so notifications
are not created again by polling or retrying a completed operation.

Validation: `tests/marketplace-trade-notifications-test.php` runs against SQLite
and fresh, automatically removed MySQL validation tables. The HTTP player suite
also exercises counter → acceptance → settlement → native companion mailbox.


## Mod and game-mode selection

The public mod directory is populated from paired mods whose player code has
been linked. Pandemonium sends the selected installation's folder ID, game mode,
verified compiled-table fingerprint and helper-exported identity definitions.
Friendly mod names never replace the folder ID used by the game. A new paired
mod appears after its first successful code link; no hard-coded website option
is needed. The browser remembers its selected mod and version locally. Linking
a code selects the detected mod and 2.4 automatically. 3.x displays Coming soon
and the API refuses trade/save operations for that version.

Agent credentials and membership are scoped by player, mod and version. Listings,
profiles, identities, wallets, gifts and settlements also include SC/HC scope.
Both parties must be enrolled in the same mod. A different compiled item/stat
catalog is refused, including a changed local catalog during link-code creation.
New mod identity definitions come from its verified helper, never the RMD-MP
catalog. Unknown mods get a conservative display catalog with no borrowed RMD
art/stat definitions. Complete localized artwork is currently supplied for RMD-MP.
Pair each new mod's companion before requesting its code; a different mod's local
pairing cannot be reused. These checks pause trading and never gate game launch.

## Free listings and gifts

A free price is an explicit marker, separate from an unpriced payment item.
The first successful claim transfers existing escrow immediately without payment
or seller approval. Concurrent claims serialize under the normal scope lock.
Discovery-only listings wait until custody has actually been established.

Gift an available escrow item from My Items, or gold/bag resources from the wallet.
The website searches same-mod linked recipients; the HUD resolves an exact name
before showing a second, stable-ID confirmation. Item gifts close existing offers,
transfer the same ledger/identity and queue delivery. Resource gifts conserve both
wallet balances atomically. Retries use the original command ID. Gifts and delivery
create the same private notifications/history as ordinary trades.
