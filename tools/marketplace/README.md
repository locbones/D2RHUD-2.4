# Unattended marketplace — D2R 2.4

The JSON-native stash marketplace connects to the authenticated PHP/MySQL API
at `https://market.d2rmodding.com/index.php`. It supports manually chosen AND
barter prices, gold and RMD-MP bag resources, private offers/messages, exact-price
automatic approval, atomic escrow settlement and guarded delivery claims.

The save agent deposits the enabled trade page after closing the stash, using
the native page-save/unload handshake. Closed-game sync remains a fallback. Priced
items become escrow listings; eligible unpriced items, gold and supported bag
contents fund offers. Listed items cannot fund another purchase. Both parties'
assets must already be in escrow for settlement. An accepted unfunded offer waits
for its buyer's next deposit. Development Trader offers never settle.

Requested returns, purchases and physical item proceeds are collected into the
delivery page while it is unloaded. View another stash page while delivery
syncs, then switch to the delivery page to load the updated contents. Closing
and reopening the stash also works.
Unspent wallet resources remain available for trades. Listed goods remain on sale while the player sleeps
or their PC is off. The site needs no resident server process: authenticated
commands and deposits trigger bounded settlement sweeps. Networking and save
writes run outside the HUD; the DLL coordinates native page unloading and short
file leases without network calls.

My Items includes physical and escrow goods. Escrow items can be repriced or
returned to delivery. Escrow wallet shows gold and bag balances. Affordability
includes unpriced physical deposit contents and available server-held funds.
The service distinguishes agreements, completed escrow ownership exchanges and
subsequent physical delivery acknowledgements.

See [custody, enrollment and recovery](CUSTODY.md),
[server administration and API](server/README.md), and the tests below.

## In-game workspace

Click the native shop button at the bottom-right of the stash footer. It is
centered vertically in the bottom strip with matching right/bottom inset. The
button reuses RMD's existing Shop_Button sprite and its normal/pressed/hovered
states. Escape or the native close button dismisses the marketplace.

The entire marketplace is now a native widget subtree in the stash JSON:
TabBarWidget, ButtonWidget, DropdownListWidget, ImageWidget, TextBoxWithLinksWidget,
TextBoxWidget, InputTextBoxWidget and RectangleWidget,
with native focus/hit testing and Escape/tooltips. Browse, My Items, Offers & Logs
and Setup use the same native tab control and RMD tab art as the stash/vendor
panels. Collections use six larger cards per page in a three-column grid.
Selecting a card opens a full-width item or conversation page; the collection
and its filters are hidden until Back. Item artwork and chat-style inspection
remain available. Quality, sorting and shared-tab
filters use native dropdowns instead of cycling buttons. Payment selection and price editing use four
rows per page. Native short input fields accept 15 bytes.
The conversation message field uses the game's owned-string setter for up to
512 characters. Item history, comparable sales, currency-specific time charts,
full terms and conversation receipts are native widgets too. Conversations
show current payment with an icon and grouped digits, a separate status, direct
Accept/Decline/Counter-offer/Withdraw actions as applicable, and a separate
message composer. Each log row separates actor/action, date, and a bounded
excerpt. Clicking a row opens the complete UTF-8 message in a paginated reader;
both line length and line count are bounded, and authored text styles preserve
newlines. Reader navigation does not discard the unsent message. Comparable item
rolls may differ. Acceptance is shown separately from a completed sale.
There are no marketplace ImGui windows, buttons, glyphs, texture uploads or
mouse interception. The HUD supplies value snapshots and handles native
BankPanelMessage commands; the game performs all marketplace drawing/input.

The 2.4 JSON resolver loads unknown `Market*` action names as zero even when
the message domain is valid, which makes a native button ignore clicks. The
stash paint hook binds the declared marketplace buttons to their custom action
hashes, including the shop button while the workspace is closed. Bindings verify
the supported native ButtonWidget click routine and message domain, and never
replace a different nonzero action. Geometry and interaction remain JSON-native.

The visual layout uses RMD's existing stone/crimson panel and divider sprites,
with native RectangleWidget frames, gold selection borders, recessed image wells
and a centered, enlarged native tab bar. The redundant routing/approval/status
strip is removed. Item cards separate the linked name,
quality and seller/payment status. The selected-item and payment areas have
distinct section headings; Setup uses two compact routing cards and separate
trading/approval rows, short captions, inline validation and a dedicated save
area above the footer. Its Refresh control is hidden; Undo changes appears only
while edits are pending. Payment
search results and price rows show nominal item thumbnails too. These are native
JSON controls and existing installed sprites, without new overlay rendering.

Every input has an explicit native gold border, recessed fill and underline;
the InputTextBox background alone is not sufficiently visible in-game. Payment
search uses a full-width labelled field and a helpful no-match state. Contextual step headings guide payment
selection, quantities and review. Empty prices explain how to start; quantity rows
are labelled. The main views use short captions rather than instruction banners;
setup instructions remain inline, with no forced
wizard or repeat onboarding; saved settings/prices keep their existing behavior.

The four native tab messages are bound separately and their selected state is
kept in sync with the controller. The close button uses stock `BankPanelMessage:Close`;
while the workspace is open the bridge consumes it as `MarketClose`. It gets
native input priority over text fields, and close requests take priority over
queued actions. Outside the workspace the stash retains its normal close action.
Native input is dispatched to the open workspace before BankPanel hotkeys. Only
the bank's hit margins expand while it is open, so the full-screen child remains
clickable outside the normal stash area; the original margins are restored on close.
Visibility transitions call native OnOpen/OnClose so text focus and button states
are released. Hidden input fields are excluded from input snapshots and resynced
when shown, including switching between price quantities and payment search.

Click an item image or bracketed name to open the same native linked-item
properties viewer used by chat links. Its input is the complete serialized
item, including socket children; the game computes the actual property text.
No chat message is posted. Native inventory artwork comes from the installed
base/unique/set HD mappings, with saved ring/charm graphic variations. Missing
artwork leaves a text card; items exceeding the native 1,024-byte link limit
keep their summary but cannot open the linked-item viewer.

While open, the marketplace consumes keyboard and raw-keyboard messages before
HUD shortcuts, stash search and the game's global hotkey mapper. Value-only
keyboard events are replayed exclusively into the native workspace on its paint
thread, with captured Ctrl/Shift state restored temporarily for native editing.
Escape closes the marketplace; captured key releases remain consumed after close.
Mouse input keeps the native path and Alt+F4/window close remains available.

Item name, quality/level and socket notes have separate fixed text rows outside
the price-editor group. Payment terms have up to sixteen separate rows in two
columns on the focused page. Visible
text and control rectangles are checked for bounds and overlap across tab,
selection, price-editor and payment-picker states. Pagination is hidden when
there is no corresponding page. These are fixture/layout checks; the native
renderer still needs an in-game appearance and interaction check after changes.

Authoring sources are layouts/stash-shop-button.json and
layouts/marketplace-workspace.json. The latter is directly editable JSON.
build-native-layout.py regenerates the defaults. Install into the active
RMD-MP layout and its remodded theme template with:

    python tools\marketplace\install-native-layout.py "C:\game\Mods\RMD-MP\RMD-MP.mpq\data"

The installer backs up changed files, preserves unrelated bytes and duplicate
JSON fields, applies atomically and is idempotent. Restart D2R after installing
the layouts and updated HUD DLL. The expanded/controller theme layouts are
not modified. The native widget bindings still require live in-game testing.

- **Browse:** your freshly priced deposit items appear immediately as **You (local draft)**
  alongside cached player and simulated-seller offers. Your own cards have no purchase
  action and are hidden by **Show things I can afford**, which filters buyable offers.
  Item cards open a full-width item/payment page; search by
  item, seller or requested payment; filter quality or affordability; sort by
  name, quality, seller or number of payment types. Test offers from the development
  seller also support a native counter-offer editor. Different barter bundles
  are not treated as a single currency.
- **My Items:** your deposit inventory across all seven shared tabs,
  tab and priced-only filters, and an
  inline asking-price editor. Select a card, then **Set asking price**. Search
  for specific uniques, sets, normal bases or runes and enter quantities.
- **Agreements:** reviewed terms tied to seller, listing ID, revision, complete
  item bytes and exact payment bundle. Changed, missing or expired offers need
  review again. These local drafts reserve nothing.
- **Setup:** separate deposit/delivery pages, a trading enable switch and an
  approval preference. Save changes applies edits. The pages must differ.
  Changing deposit explicitly clears old prices; changing withdrawal or
  approval policy preserves them.

## Retired local seller and service test trader

Setup no longer exposes a third seller page, and My Items no longer has an
inventory selector. Loading old settings disables the local simulator through
the existing compare-and-replace JSON writer, while preserving its legacy price
data and conversations. Subsequent disabled publication withdraws old test-page
listings. No `.d2i` or `.d2s` file is written.

The separate **Development Trader** service account and `test-trader.py` remain
available for testing messages, counter-offers and decisions against player
listings. These are negotiation tests only; test trades cannot create completed
sale records. Real player escrow supports automatic approval and settlement.

Deposit holds goods and potential payment. Priced goods are excluded from
spendable funds. Withdrawal is the delivery destination for purchases and sale proceeds; it
is excluded from payment inventory.

Opening or refreshing requests a native snapshot if the deposit page is
currently loaded. It verifies all seven shared containers, bounds enumeration
and serialization, and passes only value copies to the UI. Newly placed items
can appear without saving. When native capture cannot be verified, it falls
back to the last complete saved page. The panel labels live versus saved data.
It does not automatically navigate the stash. Switch to the deposit page and
reopen or Refresh for another live capture. A snapshot is not a custody receipt.

**Show things I can afford** requires every price line to match unpriced items
in the deposit snapshot. Socket children stay attached and never count as loose
payment. Stack quantities count units, not slots. Offers are checked separately;
several drafts can compete for the same payment and need service arbitration.

A price can require one Stone of Jordan, one Bul-Kathos' Wedding Band **and**
40 Zod runes. All lines are required. Two copies of the same unique should use
quantity 2 on one line. Unique/set payment requires the exact compiled identity
and identification; rolls can vary. Each line can require ethereal,
non-ethereal or either. Base/rune lines require Normal quality.

The payment picker has **Items / runes**, **Bag contents**, and **Gold** categories.
RMD-MP bag currencies use the installed mod's checksum-bound stat symbols: gems,
the four rune-point tiers, magic/rare jewels, full rejuvenation potions, keys,
organs and Set/Unique cores. Amounts are the bag's displayed units; there is no
automatic conversion to loose runes or gems. Affordability counts saved base
balances on identified, unpriced deposit bags. Listing a bag reserves its contents,
and a single price cannot combine a whole bag and bag contents.

Gold means gold stored across the designated deposit page's seven shared tabs,
not the character's carried/personal gold. Saved snapshots read tab-header gold;
loaded snapshots read each verified shared container's GoldBank stat. An
unavailable live gold balance is excluded rather than guessed. Gold and bag
amounts support 1..2,000,000,000; loose item quantities remain 1..1,000,000.
Resource prices persist a `kind` (`gold` / `bag`) and stable bag `stat` symbol;
ordinary item prices keep their previous representation. Pricing alone does
not debit a save. The guarded custody agent transfers funded resources
through the journaled escrow protocol before settlement can spend them.

Roll thresholds, OR bundles and multi-step barter chains are not implemented.
The selected-item pane shows identity and price alongside its image; clicking
the item opens native computed properties. Payment selectors describe a requested
item type rather than inventing rolled properties for that request.

Auto-trade defaults off, including migration from older settings. It approves
buyer-originated offers matching every asking term when both sides' exact
assets are available in escrow. Ownership and payment move atomically; delivery
  runs in the background while the game is closed or the destination page is
  unloaded under the live handoff, with physical capacity checks.

Settings live beside mod saves in Marketplace_2.4_SC.json or
Marketplace_2.4_HC.json, partitioned by mod and character mode. Older trade-page
sidecars remain compatible. Prices bind tab and exact parent/socket bytes;
moving or changing an item invalidates its old price. Writer leases and
compare-and-replace reject conflicting saves. Use **Setup > Reload settings**
after a conflict.

## Read-only companion and cached player offers

Build from an x64 Visual Studio developer prompt:

    tools\marketplace\build.cmd "%TEMP%\d2rhud-marketplace-tools"
    "%TEMP%\d2rhud-marketplace-tools\stash-catalogue.exe" "C:\game\Mods\RMD-MP\RMD-MP.mpq\data\global\excel" "C:\saves\Stash_SC_Page17.d2i" "C:\saves\Marketplace_2.4_SC.json" > "%TEMP%\alice-catalogue.json"

The scanner uses matching compiled weapons/armor/misc/itemtypes/itemstatcost
tables. It requires version 98, all seven tabs, enabled settings and a filename
matching the deposit page/mode. It groups socket children, rejects unsupported
or partial layouts and rechecks inputs after parsing. Failure returns nonzero
without JSON. Source files open read-only.

The envelope contains schema 1, game mode, mod, hardcore, page, capture time,
custody local_save and **purchasable: false**. Items include private serialized
bytes, location, saved stats and a price or null. Unmatched prices are counted.
Unchanged saved bytes do not prove the current in-memory state has been saved.

For a local integration preview, assemble other players' private exports:

    python tools\marketplace\cache-offers.py --seller "Alice" "%TEMP%\alice-catalogue.json" --seller "Bob" "%TEMP%\bob-catalogue.json" --output "C:\saves\MarketplaceFeed_2.4_SC.json"

Put the output in the receiving player's mod save directory and Refresh.
Hardcore uses MarketplaceFeed_2.4_HC.json. No demo offers are injected into
production. Only priced items become cards. Seller names must be distinct.
Catalogues over one hour old, mixed mods/modes and duplicate item records are
rejected. Merging does not renew source expiry.

The cache contract is schema/game_mode/mod/hardcore/expires_at plus listings
with seller/id/revision/item_hex/price. The HUD validates bounded input, expiry,
IDs and prices, and reparses serialized items using the installed mod's tables.
Agreement drafts become stale when revision, item or price changes. Imported
discovery data never authorizes an inventory debit.

Keep native exports/caches private: their companion contract includes exact
serialized items. Public website responses omit those bytes. The installed
Windows scheduled task starts companions at login and checks for a missing
worker every five minutes. Running workers poll every 15 seconds. DLL updates
remain explicit verified installations.

## Website exchange

**https://market.d2rmodding.com/** shares the game account and all trade records.
Browse without signing in. To connect your game player, use **Link player** and
enter a code from **Pandemonium → Settings → Marketplace → Get link code**.
There is no username or password to create. The browser can
price escrow or queued physical stash goods, submit/respond to offers, send
messages, read item history and comparable sales, inspect wallet funds and
change setup. Physical prices/routes apply through the guarded save agent.
See [website access, security and deployment](WEB.md).

## Installation and unattended operation

Pandemonium 0.1.6+ bundles the Windows marketplace runtime inside its own executable.
Players do not install Python, run PowerShell scripts, download a separate companion,
or receive permanent pairing files from an operator. Selecting a 2.4 mod prepares its native marketplace controls in the background.
Launching it completes player enrollment without waiting for network setup. Settings → Marketplace → Get link code also completes setup.
Start / repair restores missing runtime files without replacing the player identity.

The per-user supervisor starts at Windows sign-in, restarts missing sync/save workers,
and uses no console windows. Code, credentials, bounded logs and recovery journals
remain in `%USERPROFILE%/.d2r-marketplace`; additional mods use `profiles/MOD` inside
that root. The release contains no companion sidecars or credentials. Each setup
uses a securely saved installation secret: repeated or interrupted enrollment recovers
the same ordinary player, with separate scoped agent credentials and catalog checks.
An existing development/operator pairing is reused, never silently replaced.

The launcher repairs enrolled installations in the background. No marketplace network
request or enrollment failure blocks game launch. Pending custody journals are never
discarded during upgrades. Versioned save definitions ignore unrelated file changes;
compatible updates refresh the existing pairing automatically. New item/stat additions
can coexist, with recipient checks for older installations. Conflicting existing saved
definitions identify the affected field and preserve escrow. Each mod has its own
compatibility family. Live per-page handoff remains supported by the
RMD-MP 2.4 HUD; other mod scopes retain the closed-game custody safeguards.

  Live handoffs fence only the pages named in a pending transaction. After all
  local page replacements and their receipt are durable, those pages may reload
  while remote acknowledgement retries continue. Recovery of that applied receipt
  completes the remote commit without replaying a page over later native item
  movements. Legacy or malformed pending pointers remain conservatively fenced.
  Page selections retry short lease/cursor contention and keep the latest choice.
  RMD-MP pages are validated before the current stash is freed; a native parse
  failure explicitly restores the previous page rather than silently waiting.

The viewed delivery page refreshes automatically only for queued escrow items
or wallet withdrawals. Browsing, clicks, price edits and deposit polling never
request a visible reload. A delivery is coalesced by operation ID rather than
re-armed by a timer. Deposits continue through the ordinary save handoff when
the deposit page unloads. The native JSON shows only a small "Reloading Stash..."
label with no background. Only an acknowledged parking page grants save access;
requests bind player/process/start/session/mode/page and a delivery-only kind.
Local receipts restore the same page/tab before slow remote acknowledgement.
Gold/stack dialogs, held mouse buttons and an occupied cursor defer the handoff.
Closing the stash releases the temporary input lock; transaction fences remain.

Windows releases must use the launcher's `CrossPlatform/pack-release.ps1`, which runs
`build-marketplace.ps1`, rebuilds the save helper, bundles a checksum-pinned official
Python runtime, and verifies the runtime inside the actual published launcher. The
older `install-player.ps1` / `install-custody.ps1` tools remain for operator recovery,
not normal player onboarding. Do not distribute a developer's installed profile.

The default delivery gold ceiling is a conservative 2,500,000 per shared tab.
Bag credits require an existing supported normal Storage Bag in the delivery
page. Deliveries take only available resource capacity and try smaller item
batches when necessary. Remaining goods/funds stay in escrow. Character `.d2s`
files are never edited. Existing malformed/incomplete stash pages are refused.

TCP/IP players still own their saves and can deliberately edit or roll them back.
Custody receipts prevent accidental retries/crash duplication in the supported
flow; they are not an authoritative anti-cheat system for client-owned saves.

## Verification

    tests\run-marketplace.cmd "%TEMP%\d2rhud-marketplace-tests"
    tests\run-marketplace.cmd "%TEMP%\d2rhud-marketplace-tests" "C:\game\Mods\RMD-MP\RMD-MP.mpq\data\global\excel" "C:\saves"
    tests\run-marketplace-ui.cmd "%TEMP%\d2rhud-marketplace-ui-tests" "C:\game\Mods\RMD-MP\RMD-MP.mpq\data"
    tests\run-marketplace-native.cmd "%TEMP%\d2rhud-marketplace-native-tests"
    python tests\marketplace-bridge-test.py "%TEMP%\d2rhud-marketplace-tools\stash-catalogue.exe" "C:\game\Mods\RMD-MP\RMD-MP.mpq\data\global\excel" "C:\saves\Stash_SC_Page17.d2i"

Fixtures cover exact barter/stack matching, socket exclusion, feed/settings
validation, expiry, changed terms, routing migration, conflicts and read-only
preservation. UI checks run the native command/input protocol for navigation,
live/saved snapshots, searchable combined prices, agreements and setup. Every
published binding must exist in the authored JSON, and the marketplace must
emit zero ImGui draw vertices. Native adapter checks verify text/visibility
updates, bounded inline inputs, no per-frame typing clobber and message routing,
alongside seven-container snapshots. These do not replace an in-game test.

Browse searches offered items and requested payment by default. The scope
dropdown selects Offered + wanted, Offered items, or Wanted payment. Browse
quality filters (and website category filters) apply to the matching side of
the trade, so a unique ring asking for Zod Runes is found by a wanted-rune
search. Payment names resolve from the item catalog, including gold and bags.

Windows startup/recovery uses pythonw.exe and a console-free PowerShell child.
The save helper also uses CREATE_NO_WINDOW. Startup diagnostics remain in
`~/.d2r-marketplace/startup.log`; existing sync and worker logs are retained.

For player troubleshooting, use **Pandemonium → Settings → Marketplace →
Copy support report**. Send that one report instead of collecting individual
status or log files. It combines installation and transfer state, native stash
reload failures, automatic checks of every shared-stash page, launcher activity,
and recent HUD/companion errors. Credentials and save contents are excluded;
collection is read-only and runs only when requested. Repeated errors are
condensed. New companion activity uses one tagged `diagnostics.log` with one
size-limited previous log; old worker logs remain readable during upgrades.
Recovery journals remain private internal state and should never be deleted as
part of collecting a report.

Player controls are shared by the HUD and website: edit the public name in
Setup, search by seller, save individual trades or sellers, filter favorites,
hide your own trades, or exclude trades with active offers from other bidders.
Names use 3–15 letters, numbers, spaces, dots, underscores or hyphens and must
be available. Renaming preserves the player ID, custody, favorites and history.
Favorites are private and separate for Softcore and Hardcore.

Each hosted trade has a paginated offer board with exact barter bundles and
status. Anonymous bidder labels remain stable within that trade. Visitors
receive no bidder IDs, real names, conversation IDs or message bodies. Only
the two participants can open their private conversation. The website seller
can accept, decline or counter directly from the comparison cards; the HUD's
Review offer control opens the native conversation with the same actions.
Public offers include closed proposals with their status; only pending or
accepted offers from another bidder affect the browse exclusion filter.


## Player reports and moderation

The website exposes **Report seller** on a trade and **Report player** on public
profiles. A code-linked player chooses a reason and writes an explanation. The
service captures the exact listing revision or character snapshot, including a
selected equipment/inventory/mercenary item when relevant. Reports never mutate
trades, item ownership, balances, or save files. Evidence excludes raw save bytes.

The private Moderation tab supports an active/all/status queue, player/reference
search, frozen evidence, identity matches and recorded sales, status changes and
append-only private review notes. Pending reports are deduplicated; submission
quotas and revision checks protect retries and concurrent reviews. Guest and
ordinary-player APIs cannot read cases or notes.

Moderator access is an independent community capability on a stable player ID;
the actor remains a player and keeps existing linking/trading sessions. The
operator-only command is `php cli.php moderator PLAYER_ID`. There is no remote
assignment command or assignment UI. Production Bonesy was granted this
capability after verifying the enrolled stable ID; profile names never grant it.

The in-game interface prioritizes core trading: six wider cards per page, a
separate Filters page, bounded summaries, four payment rows per page with icons,
and a paginated full-price/message reader. Seller favorites and seller discovery
remain available on the website. All marketplace controls are native JSON
widgets. The layout generator preserves legacy command names in hidden groups
where needed; install matching HUD and layout files together.

Run `marketplace-moderation-test.php` and `marketplace-moderation-http-test.py`
for permission, evidence, idempotency and session/CSRF coverage. The isolated
MySQL validation harness includes a `moderation` suite and rewrites all three
report tables to disposable namespaces. Native UI checks capture presentation
states for `marketplace-layout-text-test.py GAME_DATA PREVIEW_DIRECTORY`, which
measures captions using the installed Exocet font. Rendered layout previews are
illustrations and do not replace in-game visual verification.


## Owner demonstrations, resets and market statistics

The website now has a public **Trade stats** page: the ten latest completed
trades, popular item identities and seller rankings, with 7-day, 30-day and
all-time views. Each view follows the selected mod and SC/HC mode. Item images
open the usual property tooltip, and each recent sale shows its original price.

Bonesy's stable linked player ID alone has **Owner tools**, beside the mod
selectors. Generate a private Trading party, Busy market or Trade festival,
then choose **Enter demo**. The fake traders negotiate and reply inside this
website-only workspace; its items, balances, messages, profiles and sales remain
separate from the live site and game saves. A reload returns to the live market.
The demo includes 24–100 fake players, 100–600 listings and 80–480 completed sales.

**Clear demo** removes only that generated workspace. **Clear marketplace**
first pauses trading and returns real escrow through the normal companion.
After all holdings and save operations clear, type the final confirmation.
Real player links, names and permissions remain. A private recovery snapshot
and technical exactly-once delivery receipts are retained. The updated HUD and
companion clear stale local trade drafts after reset, preventing old listings
from reappearing automatically. The reset has not been run on the live market.

### Single market page (0.1.12)

The existing sale page becomes the market page; `withdraw_page` is retained in
the wire format and equals `page` in new setups. Legacy sidecars are backed up
before route migration, and unfinished transactions recover before migration.
Only ordinary items with asking-price drafts enter escrow automatically.
Unlisted goods stay local until used to fund a mutually agreed trade. Gold and
unpriced bag currencies still auto-deposit when the page is saved/unloaded.
Delivery claims use the same guarded page lease and occupancy checks.

Bag-currency claims create normal Z01 containers with claim-bound UUIDs
and only the requested balances, splitting large amounts across available slots
using the installed mod's stat definitions.
No existing bag is changed by a currency delivery. Completed delivery receipts
retain returned gold and unchanged delivered bags so background sync cannot
sweep them back into escrow. Taking gold out lowers its retained amount; taking
a bag off the market page releases its exemption after the saved page syncs.
Red cell borders mean unlisted; green borders mean received. Border receipt
reads run in the background, and native serialization is cached and amortized.

During an acknowledged delivery refresh, the companion processes only queued
returns and wallet withdrawals. Listing/setup publication, deposits and optional
character/browser-link uploads resume afterward. Reservations, write authority,
save leases, checksums and crash-recovery rules are unchanged. One compact
`delivery-timing.json` diagnostic records request durations after each handoff.
The native stash notice uses large gold text centered on a local black backdrop;
item borders fade inward to avoid tinting neighboring cells.

## Player-facing Vault and notification audio

The HUD's Vault tab and website Vault navigation contain Items, Gold & resources,
withdrawals and activity. The custody protocol, SQL tables and compatibility
routes retain their escrow/wallet identifiers; this is a presentation change,
not a transfer or migration of player assets. The native layout generator also
replays `layouts/marketplace-single-page-patch.json` so rebuilding does not restore
the obsolete separate deposit/delivery Setup controls.

Personal trade alerts are limited to participating buyers/sellers. Saved-search
listing notices have their own category and require explicit opt-in, including
for accounts created when notifications defaulted to all enabled. Companions
also classify legacy queued saved-search notices independently of trade alerts.

HUD Settings → Sound → Notification volume controls chat and marketplace sounds
from 0–100%, defaults to 30%, and persists in the HUD preferences. Preview uses
the same gain. A bounded background decoder and XAudio2 source voice keep file
I/O and decoding off the game thread; missing/unsupported sounds use a quiet
generated chime at that gain. Notifications never change the game/system mixer,
launch an external media player or use the unattenuated system beep.

Companion manifests include an internal `build_epoch` for fixes within an app
version. Signed caches older than the launcher's embedded build cannot replace
it. App version changes remain an explicit release decision.

The native marketplace uses its own three-state marketplace button artwork in
`assets/`. Only the HD and low-end sprites are installed, under the mod's
`data/Pandemonium/Marketplace/`; developer PNGs stay in the repository. The
installer backs up and atomically publishes artwork and layouts together, and
rolls both back on failure. `build-native-layout.py` fits controls to the visible
InfoBGW_R area (x10..2238, y60..1558), rather than its transparent padding, and
keeps button hit rectangles and centered captions equal to the sprite frames.
`tests/marketplace-sprite-layout-test.py` checks those real artwork dimensions
and installation/rollback behavior; the native UI tests check all presented
pages against the visible panel bounds.

### Hellfire Torch class requests

Select Hellfire Torch as a payment, then choose Any class or one of the seven character classes. The website supports the same choice on wanted listings. Prices, counter-offers, history, affordability, agreed funding and Vault settlement retain the class requirement. Matching uses the identified root item’s positive native class-skill property (stat 83), excluding socket/set lists and ambiguous multi-class rolls. Existing prices without `class_id` accept any class. The unique item index remains catalog-specific. New HUD support is advertised by its signed companion manifest; older HUDs cannot consume a class-restricted feed or auto-fund it with the wrong class.

### Community gift campaigns

Owner tools → Community gifts creates an item bundle from 1–16 identified items
on the configured market page or in the Vault, including mixed bundles. Market-page
items must be unpriced and synced by the companion; no asking price or Vault deposit
is required. The picker shows both locations by default and refreshes newly synced items
without clearing the gift form, selected currencies or existing selections.
The source selector preserves selections when switching locations; Refresh items
can request an immediate snapshot without rebuilding the form.
Templates are frozen at publication; originals stay where they are and remain yours.
Stale, consumed, foreign-player, other-page and pending-price selections are refused.
Native gift visibility uses the frozen template, so moving the original later does
not hide an existing campaign.
The owner may set an announcement, a message revealed on claim, and an optional
claim deadline. Campaigns belong to one mod, game mode and SC/HC scope. Any linked
account, including an account linked after publication, may claim before expiry.

The same gift can include up to 16 gold/bag-currency lines, with a quantity per
account. Currency-only gifts are supported. Only currencies in the selected mod
are offered. Owner balances are not spent: these are owner-created rewards.
Claimed currencies credit the player’s Vault immediately and can be traded or
withdrawn using the existing safe gold/bag delivery flow. Item copies remain
queued for normal delivery. Both resources and items appear in the gift preview
and owner claim audit. Older HUD builds omit resource campaigns until updated.

Claims are transactional and unique per campaign/account. Retry commands return
the original receipt. Every generated item and socket child gets a fresh native
UUID; extended items also get a new seed. Saved properties and stack quantities
are preserved using the verified mod save definitions. The whole bundle enters
the claimant’s Vault and uses normal queued delivery, including safe waiting for
space. Owner claim history reports each recipient UID and delivery receipt.

The website announces new gifts with a dismissible banner and a gold gift badge.
The HUD uses native JSON controls, item links and the same private chat pipeline
for announcements, respecting notification volume. Native campaign controls are
hidden by default for older HUD builds. Ending a campaign prevents future claims
without recalling gifts already claimed. Tests never publish live campaigns.

Regression coverage: `marketplace-gifts-test.php`, the MySQL validation runner’s
`gifts` suite (including simultaneous claims), `marketplace-gift-codec-test.cpp`,
and native UI geometry/font checks. The codec is tested by the independent native
save parser for both synthetic and read-only real-item fixtures.

### Marketplace shortcut

D2RHUD Options → Hotkeys & Commands → Open Marketplace accepts the same key or
modifier chord as other HUD shortcuts. It starts unbound. From normal gameplay,
the shortcut shows only the marketplace, waiting for a fresh panel capture. Its
native stash host and inventory are visually hidden, without changing item
records or their native lifecycle. Closing the marketplace restores the previous
stash/inventory visibility and closes only panels opened by the shortcut. Repeated keydown
messages coalesce. Chat, modal item interactions, a held item, death and live stash
handoffs cannot force a panel switch. Unsupported clients/layouts decline safely.

The window callback posts a request; native panel lifecycle calls stay on the
existing UI/render path. The idle path performs no file I/O, item serialization,
or stash inventory copying. `marketplace-hotkey-test.cpp` covers lifecycle policy;
`marketplace-hotkey-native-test.cpp` checks the adapter against installed 2.4 code
signatures with controlled native calls, including changed-signature rejection.

An explicit gold deposit on the open market page is recognized only when carried
gold falls by exactly the amount added to the shared page. The native client then
saves and parks that page using the normal fenced handoff, the companion transfers
gold through its durable transaction journal, and the same page/tab reloads.
A fresh manual deposit releases an older withdrawn-gold holdback; an incoming
withdrawal by itself never requests a deposit. Optional browser linking and
character preview work runs independently of this transfer path. Stash highlights
are invalidated on the native close event rather than waiting for snapshot expiry.
