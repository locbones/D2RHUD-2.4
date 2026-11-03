# Tests

Run the Windows C++ fixtures from an **x64 Native Tools Command Prompt for
Visual Studio 2022**. Restore the dependencies in
[../d2rhud/vcpkg.json](../d2rhud/vcpkg.json) first. Use a temporary directory for
compiled test output; build products and caches are ignored by Git.

## Examples

Mercenary skill icons (dependencies include CascLib and zlib):

```bat
tests\run-merc-icons.cmd "C:\path\to\x64-windows-static" "%TEMP%\d2rhud-merc-icons-tests" "C:\path\to\D2R.exe" "C:\path\to\D2R" "RMD-MP"
python tests\merc-icons-metadata-test.py
```

Checks source parsing, frame zero, skill-description links, all six slots in
the shared native widget, custom IDs outside the retail switch, default-on and
saved-off settings, TXT/BIN-only metadata, layer isolation and native fallback.
The executable fixture installs the real hook in a private PE image, preserves
tooltip identity, required levels and colors, verifies native tables stay
unchanged, and unwinds across the relay. No game is started or controlled.
Optional install/mod arguments audit the installed column mappings read-only.

Mercenary seller authorization and native client/host hooks:

```bat
tests\run-merc-seller.cmd "%TEMP%\d2rhud-merc-seller-tests" "C:\path\to\D2R.exe"
```

Checks Act IV and custom sellers, Classic/Expansion and all difficulties,
first-row filtering, invalid and oversized name ranges, default-on settings and saved opt-outs,
preservation of NPC services, localized Hire entries and native roster packets.
The executable argument also installs the actual hooks in a private PE image,
exercises the native roster/purchase/refresh capability gates and roster
initialization, and verifies stack unwinding across all six relays. A complete
native action-3 request reproduces Tyrael2's Class 944 Crusader definition and
67-name range, then checks ten normal roster replies reach the native client
offer slots. Unrelated NPC actions and invalid interactions remain rejected. It
also runs the real preview and Buy name lookups and the native price calculation
and purchase packet handler. Shared and unique name ranges produce matching
client/server quotes; Class 944 creation, one charge and attachment are checked
with isolated effect stubs. Wrong prices, insufficient gold and duplicate buys
are rejected before effects. Optionally append a read-only captured hireling BIN
to the command to reproduce the loaded mod's rows exactly. It does
not start D2R, access a running game, purchase mercenaries or write saves.

Dynamic monster automap CEL icons (optional executable argument verifies the
native signatures and runs the audited CEL renderer/projection in an owned,
offscreen image):

```bat
tests\run-dynamic-automap.cmd "%TEMP%\d2rhud-dynamic-automap-tests" "C:\path\to\D2R.exe"
```

Checks discovery, movement, death/unload, refresh timing, layer changes, shared
object sprites, corrupt-snapshot fallback, native-data preservation and per-mod
settings. Includes a synthetic 2,048-monster refresh benchmark; it does not start
D2R or access a running game.

Marketplace prices, read-only seven-tab stash parsing, and durable JSON sidecars:

```bat
tests\run-marketplace.cmd "%TEMP%\d2rhud-marketplace-tests"
tests\run-marketplace-ui.cmd "%TEMP%\d2rhud-marketplace-ui-tests"
tests\run-marketplace-native.cmd "%TEMP%\d2rhud-marketplace-native-tests"
```

Optionally append a compiled Excel directory and save directory for a read-only
audit of real 2.4 stash pages. See [marketplace setup](../tools/marketplace/README.md).
The UI fixture checks the JSON-native command/input protocol, four sections,
pricing/search, affordability, agreements and setup, with zero marketplace ImGui
vertices and complete JSON binding coverage. Optionally pass a mod data directory
to verify installed native controls. The adapter checks ownership, seven-tab
snapshots, text/visibility updates, input revisions and native message routing.
It also reproduces the 2.4 JSON loader's zero custom-action hash, verifies that
the closed workspace binds its shop/close controls, and protects foreign widgets
and existing native actions from rebinding.
Native fixtures cover the four TabBarWidget message records and selected index,
stock close routing, modal input dispatch and exact restoration of bank hit
margins. The UI fixture checks visible text and control bounds/non-overlap in
all four sections, item selection, price editing and payment search, and keeps
item identity outside the editor's hidden detail group. These checks do not
replace a live native-renderer appearance check.

Queue tests take the static dependency directory and an output directory:

```bat
tests\run.cmd <dependency-directory> "%TEMP%\d2rhud-queue-tests"
```

Native compatibility suites also accept the local game executable:

```bat
tests\run-cube-map.cmd "%TEMP%\d2rhud-map-tests" "C:\path\to\D2R.exe"
tests\run-candy-corn-save.cmd "%TEMP%\d2rhud-candy-tests" "C:\path\to\D2R.exe"
```

The Candy Corn suite optionally accepts a compiled Excel data folder and a
save folder as its last two arguments. It reads `.d2s` characters and `.d2i`
stash pages without writing them; conversions are tested in memory.
Legacy characters are also run through the real installed preview and game-load
detours against those compiled tables, with the native consumers checking the
exact converted bytes and the original files/buffers remaining unchanged.
Legacy stash pages also run through the installed stash detour, checking every
converted tab, the complete original backup, and restored native descriptors.
Dense mixed old/new Candy Corn tests preserve all 549 Halloween random rolls
and existing quantities, reuse failed continuations on malformed tails, and
continue refusing genuinely ambiguous records.
Valid current-format stacks without RNG properties also pass unchanged, both
alone and alongside legacy items. Current-format character fixtures run through
the installed preview/game-load detours without replacing their native buffers.
Socketed runeword gear is tested across all 0-7 serialized child counts and
0-15 saved socket totals. Migration preserves the parent, children and item
properties byte for byte even when the counts differ; missing child records
still fail. Shared-stash failures report the tab and furthest parsed item/offset.
The preview hook runs before last-selected-character inventory parsing, uses
the native shared-lock buffer layout, and preserves all five reader arguments.
Its tests cover native empty-buffer return, lock release, backup preservation,
native rejection and exceptions. Migration failures use a native basic preview
that skips inventory decoding, keeping valid character headers in the selection
list even when item metadata, detailed parsing or backups are unavailable.
The installed native reader detour is exercised both with and without the
optional outer load scope, including different reader buffers/output slots and
another client's read. Failed backups return the native bad-save result without
parsing incompatible bytes. Native reader rejection and later load failures retain
their result codes in `last-conversion.txt`; original saves/backups stay intact.
Shared-stash tests cover multiple tabs, mixed legacy/current stacks, nested
items, gold/metadata preservation, tab lengths, complete `.d2i` page backups, and
restoring native descriptor ownership after success, rejection and exceptions.
The real native stash-loader body also validates the converted descriptor lengths
and item-list arguments with world allocation/stat/network side effects stubbed.

Loot filter tests can use this repository's engine:

```bat
tests\run-lootfilter.cmd <dependency-directory> "%TEMP%\d2rhud-filter-tests" <filter-examples-directory> lootfilter.lua
```

UI fixtures may need extracted mod artwork, fonts, or table paths. Read each
`run-*.cmd` script for its arguments and dependency directory. Some scripts take
an explicit dependency directory; others use the existing repository-local
vcpkg installation layout.

## Coverage

`run-cube-map.cmd <output> <D2R.exe> [compiled-excel-folder]` also checks
terrain-independent random rosters, custom/inherited aquatic monster rows,
spawn and secondary-minion dependencies, recursive families and safe flying
monsters. Optional compiled tables are read into private copies to test actual
BIN-only rosters and verify the original files remain unchanged.

`run-quest-journal.cmd <output> <cubemain.txt> <quests.json> [existing-journal-folder]`
simulates a host and independent client storage folders. It checks immediate
client-owned writes, acknowledgements before another claim, host-held legacy
guest recovery, old timestamp/checksum histories and definition drift, all 45
reward gates, bounded file counts across repeated starts, writer conflicts,
stale/forged commands and recovery after a failed write. Optional existing
journals are replayed through isolated copies and verified unchanged.

`run-hud-localization.cmd <output>` tests every bundled language pack, unsafe
format rejection, English fallback, stable control IDs across languages,
localized search, actual resource loading and private preference/override files.
It also rasterizes the bundled multilingual font and checks full Japanese,
Korean and Chinese production font atlases against DX12 texture-size limits.
`node tests/translation-editor-test.cjs` checks the offline editor's import/export
logic; append `--browser <output>` with Playwright/Edge available for actual UI
and download checks. HUD-menu and quest-journal UI fixtures accept an optional
language JSON after their existing arguments.

Suites cover configuration, UI layout/input, loot filters, item serialization,
stack splitting/socketing, saved companions, cube maps/events, quests,
Whirlwind, extended tables/classes/acts, and native compatibility guards.
Native fixtures use private executable mappings and bounded engine stubs;
they do not replace live gameplay and multiplayer checks.

`run-quest-recipe-source.cmd <output> <mod-data>` exercises the installed BIN-only
recipe annotations. Unrelated recipe edits, appended rows and stale BIN
checksums/sizes must not disable quests or change quest definition identity.
It uses the project's CascLib/zlib paths; set `CL`/`LINK` if these dependencies
are outside the restored package directory. Native cube tests check appended
ordinary recipes and actual quest gate mismatches. Journal tests cover progress
reload, rollback, duplicate rewards and missing-source diagnostics.

Act VI startup tests call the production `Install` and `Configure` entry points
with real files, including `"enabled": True`, `False`, truncated manifests,
missing manifests and malformed preferences. Cases cover both preferences,
five/six/unknown loaded acts and BIN/TXT launches. Five-act BIN games tolerate
unused optional-data errors, while requested TXT/loaded-six-act validation stays
strict. Files are preserved. The full startup/switching fixture also runs with GitHub LF line
endings against the original Windows manifest hashes; content edits still fail.
Startup-loader tests verify feature-specific rejection reasons,
initialization-stage timeouts, diagnostic exports and older-DLL fallback. A
BIN-only Act VI fixture skips the separate TXT package-authoring check.

The `generate-*.py` scripts regenerate native compatibility headers from a
matching executable. These tools require Python with `pefile` and `capstone`.
Keep the generated headers in source control: they are build inputs, unlike
compiled test executables and captured test output.

## Legacy Candy Corn client-preview replay

```bat
tests\run-candy-preview-replay.cmd "%TEMP%\candy-preview-replay" "C:\path\D2R.exe" "C:\path\data\global\excel" "C:\path\TryingAgain.d2s"
```

Replays a legacy or already-current character through the installed preview hook and the
actual native preview header/stat parsing and item-list traversal. Appearance
string/quest storage and the final item decoder are stubbed; every item byte
reaching that decoder must match an independently converted character. The
fixture verifies the original file, cache pointer/length, shared lock and exact
backup, and never writes the source character. Test backups use a private
`LOCALAPPDATA` under the output directory.
The native basic-preview fallback must preserve summary/progression fields and
skip every item decode on metadata, backup or body-validation failure; native
rejection of a truncated header remains intact.

## Extended cube comparisons

The cube-visual native fixture checks every three-condition AND/OR truth-table
combination, default AND and unused slots, malformed columns, option-off fallback,
recipe ordering, native player/item comparisons, and journal isolation. Two
interchangeable first-input candidates verify that separate items cannot satisfy
opposite halves of AND and that `useitem` selects the qualifying item.
Mixed-rune deposit probes preserve actual source item stats in private views.
The actual native server inventory request routine is replayed in prepare and
commit modes: rejected conditions send/move/create nothing, successful prepare
uses the native inventory-publication path, and commit rechecks current stats.
Existing native output/save/publication and rollback fixtures run afterwards.
These are private native-image replays, not a launched live TCP/IP session.
`generate-cube-condition-native.py` regenerates the additional ingredient
matcher relocation and chained Windows unwind metadata from the supported EXE.

## Indexed and multiple useitem outputs

The cube-visual fixture executes the native linker for all seven input selectors
in all three output slots, including BIN marker round trips. Native matcher and
creation replays cover independent A/B/C copies, original GUID preservation,
quantities, socket children, separate modifiers, selected-source metadata,
option-off and missing/stale source rejection, and copy/preflight/placement
cleanup. Quest event proof-slot removal retains original input numbering.
Both phases of the actual native server inventory request are replayed, checking
ingredient moves, commit consumption, fresh IDs and full new-item publication.
Save/load, socket, inventory and packet dependencies use controlled fixture
boundaries. These checks do not replace a live multiplayer gameplay test.
`generate-cube-useitem-native.py` regenerates output-routine relocation,
signature and Windows unwind metadata from the supported executable.

Soul Surprise admission resolves its own named root and reports a missing
`SoulSurprise` table before arena setup. Native loot replay follows the exact
negative-pick root through its weighted chest to all four fragment outcomes,
including simultaneous Treasure Trove, Golden Goose and Plentiful bonuses,
and verifies that the shared treasure-class records remain unchanged.

Soul Surprise return tests require a confirmed fragment drop, no living event
monsters and no S02-S05 items on the event floor or in a drop animation. They
cover all four codes, bonus duplicates, cursor/inventory exclusion, area scope,
server units not yet linked into rooms, delayed/missing drops and late
loot/revival cancelling queued returns. The two-second clear delay starts after
pickup, while ordinary maps retain their existing completion behavior.

## Installed loot-filter library

`tests\run-lootfilter-library.cmd "%TEMP%\lootfilter-library-tests" [game-directory]`
checks My Filters precedence, current Pandemonium and legacy D2RLAN packages,
extracted mod data, packaged configs, Lua extension casing, fresh imports and
exclusion of sound/update/archive entries. Every listed filter must resolve to
an applicable file. The optional installed RMD-MP package check is read-only.

Native marketplace visuals and filter bindings:

```bat
tests\run-marketplace-native-links.cmd "%TEMP%\marketplace-links"
tests\run-marketplace-native-images.cmd "%TEMP%\marketplace-images"
tests\run-marketplace-dropdown.cmd "%TEMP%\marketplace-dropdowns"
tests\run-marketplace-item-visuals.cmd "%TEMP%\marketplace-art" "C:\game\Mods\RMD-MP\RMD-MP.mpq\data"
```

These cover exact serialized link bytes/socket payload bounds, native string and
resource ownership, inventory graphic variations and unique/set mappings, and
revision-aware dropdown selection. The UI fixture also checks the six-card grid,
linked item presentation and filter input bridge without generating ImGui vertices.
Native appearance still requires a live in-game check.

The UI fixture checks the compact two-route Setup page in saved, edited and
invalid-page states, hidden header/footer controls, contextual Undo changes,
and absence of local seller controls. Loading old enabled simulator settings
disables that route while preserving old prices and conversation logs; repeated
loads are idempotent. Legacy conversations remain readable. Core serializer and
publication fixtures retain legacy simulator-format coverage.

`run-marketplace-custody.cmd OUTPUT [MOD_DATA SAVE_FOLDER]` checks pure stash
transformations: exact whole items/socket payloads, stack splits, gold and
existing bag balances, protected asking-price items, grid limits, corrupt input
and optional byte-identical round trips of real complete pages. It never writes
source saves. `python tests/marketplace-custody-workspace-test.py HELPER MOD_DATA
SAVE_FOLDER OUTPUT` runs the durable journal on copies of real serialized items,
with four actual child-process crashes, multi-page recovery, ID retries, writer
exclusion, restored/corrupt pages, stale table fingerprints and unchanged source
hashes. No service request, escrow claim or completed sale occurs.

Own-listing regressions cover a new price appearing in Browse before any cache
refresh, exact item/price updates and removal, player-feed preservation, local
listing labels, affordability filtering and refusal to buy one's own listing.

The native marketplace suite verifies ordinary-label allocation ownership during
updates and destruction. Text is assigned through the native string setter, byte
length excludes the terminator, unchanged paints do not reassign, and cleanup
frees only native allocations. This guards against replacing an owned caption
with HUD/static memory during marketplace updates.

Graphics resource exit lifetime regression (real ATL COM owners, fake probes):

```bat
tests\run-process-lifetime.cmd "%TEMP%\d2rhud-process-lifetime-tests"
```

Child processes verify that process termination avoids late COM release while
explicit reset releases resources normally. A normal-static positive control
delays on Release. No game or real graphics driver is launched.

Production DirectX startup/reset smoke test (headless WARP, no game/window):

```bat
tests\run-d3d12-reset.cmd "%TEMP%\d2rhud-d3d12-reset-tests"
```

The generator extracts the production resource types, lazy getter and ResetState
verbatim. The test starts with an empty reset before any resource-state access,
then creates real DX12 owners and resets/refills three times. It checks all
retained owners/handles and device validity, including texture array slots.


Marketplace activity and service tests:

```text
tests\run-marketplace-activity.cmd "%TEMP%\marketplace-activity"
php -d extension=pdo_sqlite tests/marketplace-server-test.php
php -d extension=pdo_sqlite tests/marketplace-publication-test.php
python tests/marketplace-sync-test.py PATH_TO_PHP
```

The activity tests cover completed-sale validation, seed collisions and private
threads. The PHP/PDO SQLite and HTTP companion tests cover authentication, exact
prices, stale revisions, idempotency, single acceptance, resale lineage, history
pagination, bounded logs and save-file preservation. Native/UI tests also cover
long owned-string inputs, history/receipt views, graph rectangles, local/test
conversations and queued service actions. Deployment checks additionally passed
against DreamHost MySQL using connection-scoped temporary tables, then against
the live HTTPS API and PC sync wrapper. All 31 game-save hashes were preserved.
In-game visual testing remains a separate verification step.

Publication tests cover authenticated owner/mode boundaries, stable IDs on
retries, stale captures/revisions, price-change and withdrawal logs, expiring
listings, accepted-negotiation invalidation, seed collisions and the prohibition
on completed sales involving development traders. HTTP tests publish native
player/test-seller envelopes, refuse mismatched asking prices, and exercise the
development trader's proposals, messages, counters and decisions. UI tests
verify live publication exports, deliberate disable vs failed-scan behavior,
hosted/local card reconciliation and actual proposal commands.


Website marketplace integration:

```text
php -d extension=pdo_sqlite tests/marketplace-web-test.php
python tests/marketplace-web-http-test.py PATH_TO_PHP
python tests/marketplace-agent-test.py PATH_TO_PHP PATH_TO_HELPER MOD_DATA SAVE_FOLDER
```

These cover one-use player codes, code refresh, rate limits, CSRF,
HttpOnly sessions, guest/private separation, mode boundaries, exact saved
properties, queued physical pricing/setup, consumed-item suppression and
counter-offer acceptance with atomic settlement. The agent test additionally
uses copied native saves for website pricing/deposit and all five process-loss
boundaries, preserving originals. Website domain tests also pass on DreamHost
using connection-scoped temporary tables. UI fixtures remain local only.

Marketplace item identity: `marketplace-identity-test.php` tests persisted UUIDs,
legacy seeds, moved/restored copies, cross-account admission, ownership/history
continuity, stack fragment identities, and additive metadata repair.
`marketplace-identity-parity.py PHP HELPER MOD_DATA SAVE_FOLDER` compares the native
and PHP readers on read-only stash pages, including socketed items.

Gameplay availability: `python tests/marketplace-game-gate-test.py` exercises a
real Windows game lease acquired during cloud authorization, deferred journal
recovery, sidecar exclusion, changed-save preservation and process fallback.
The native `marketplace-launch-guard-test.cpp` and launcher `--custody PYTHON`
checks verify pending/corrupt/offline marketplace state cannot reject gameplay.

Persistent returns: the browser domain test covers repeat discovery, cancelled
capacity reservations and selected claims preserving other payment goods/funds.
The agent HTTP test also returns a real serialized item without a launch marker,
checks its native properties, retains wallet gold and verifies no repeat delivery.

Live stash handoff: `tests\run-marketplace-live-window.cmd GAME_EXE` checks the installed PE's exact callback bytes/vtable slots and native
parking acknowledgement, cursor safety, short writer gate, pending-page freeze
and unrelated-page navigation. `python tests/marketplace-live-agent-test.py`
checks real Windows process identities/leases, heartbeat expiration, route and
mode isolation, multiple-game refusal, immediate named-event wake and a
reopen racing cloud authorization. The HTTP agent test also deposits gold and
returns a serialized ring while an owned disposable D2R-named process stays
alive, preserving wallet funds, item properties and exactly-once recovery.

Marketplace search: `node tests/marketplace-search-test.js` checks the shipped
browser predicates for offered/wanted/both, canonical payment names, scoped
quality/category filters, gold and bag contents. The native UI suite also
checks these barter directions and the scope dropdown's binding and geometry.
`python tests/marketplace-background-test.py` checks an actual Windows helper
child and pythonw entry point have no console and use CREATE_NO_WINDOW.

`marketplace-social-test.php` checks public-name conflicts and stale edits,
favorite owner/mode isolation, anonymous offer pagination and alias stability,
private conversation permissions, counters and absence of third-party names,
IDs or messages. The HTTP sync test sends the HUD's profile/favorite/offer-page
commands through the real service and retires old page payloads without
replaying commands. Native UI tests exercise four offer comparison cards,
public full-price review, private negotiation routing and all new controls.
`node tests/marketplace-collection-test.js` checks collection pagination,
independent list Lines and tile Size controls, My Items paging, escaping,
compact actions and access to full mixed barter prices without tall rows.

`node tests/marketplace-item-names-test.js` checks localized magic and rare titles,
unique/set upgrades and artwork, unidentified masking and unknown-ID fallbacks.
`marketplace-item-names-test.php` checks saved name IDs with optional header
fields, UUID/seed parity, old public listings and private inventory projections
without exposing custody bytes or names for unidentified items.

Wallet validation: `marketplace-wallet-test.php` exercises atomic reservations,
input validation, command replay, player/mode isolation, partial claims,
cancellation before/after authorization, held-fund trade blocking and exact
settlement after release. `marketplace-mysql-validation.php SERVER CONFIG`
runs that suite in new prefixed MySQL tables, then races two withdrawal workers
against one balance and removes only its validated test tables. Run it from a
private CLI directory containing the wallet test. No real service tables are
read or changed by this harness. The agent HTTP suite also tests a real copied
Storage Bag, missing-bag waits, partial gold capacity, all five interruption
points for wallet delivery, explicit returns and no implicit wallet sweep on
launch. Original source saves are verified byte for byte. Native UI tests check
wallet browsing, amounts/review/cancellation, complete widget bindings, visible
bounds/non-overlap and zero marketplace ImGui vertices.

`marketplace-transaction-test.php` validates bounded rollback/retry for database
contention, one final commit and unchanged validation errors. The MySQL harness
also races two funded buyers for one item and checks one ownership transfer and
one exact payment. Scope acquisition uses an exclusive no-op upsert to avoid
shared-lock upgrade deadlocks from concurrent INSERT IGNORE calls.


Player profiles and native messaging:

- `marketplace-character-profile-test.py HELPER MOD_DATA SAVE_FOLDER` checks
  immutable character parsing, checksum/truncation refusal and stash exclusion.
- `marketplace-players-test.php PROFILE_FIXTURE` checks public code-linked
  characters and private direct/trade messages, idempotency and mode ownership.
  The prefixed MySQL harness accepts `players PROFILE_FIXTURE` after its normal
  server/config arguments to run the same suite without production table access.
- `marketplace-players-http-test.py PHP HELPER MOD_DATA CHARACTER_D2S` exercises
  the actual read-only publisher and HTTP browser/native mail paths, corrupt-save
  retention, manifest removal, bounded offline backlog and cross-mode replies.
- `run-marketplace-chat.cmd` checks command parsing, packet pass-through,
  durable display dedupe, callback re-entry and acknowledgement after display.
- `marketplace-chat-native-test.py D2R_EXE` audits installed native entry contracts
  offline. The live presentation check must also run outside Present: the captured
  UI/font resource deadlock is documented in `native-chat-evidence.md`.

The live HTTPS test on 2026-11-01 displayed a Development Trader message in
native Game chat and delivered `/mr received` back to the website. Profile sizing
was checked in the browser with matching equipment/inventory panel heights.


Marketplace free/gift and mod scopes:

- `marketplace-transfers-test.php`: canonical free prices; payment-free first
  claim; private item gifts with unchanged ledger/history; gold/bag conservation;
  recipient/owner validation; rollback and command retry behavior.
- `marketplace-mods-test.php`: catalog-bound paired mod registration, detected
  link-code scope, dynamic directory, scoped profiles/pairings/identities/escrow,
  cross-mod claim/gift rejection, additive migration and 3.x refusal.
- `marketplace-scopes-http-test.py PHP`: real cookie/bearer endpoint routing,
  paired link context, mod directory, conservative per-mod catalogs, explicit
  mod/version selection and wrong-mod companion refusal.
- `marketplace-free-history-test.js`: free/gift history stays visible while
  giveaway markers are excluded from paid-currency charts.
- Native UI tests include complete free/gift button bindings, amount validation,
  resolved-recipient review, duplicate-submit protection and visible failure
  feedback. Chat tests cover direct replies to resource-gift notifications.
- MySQL validation supports `transfers` and `mods`, always using disposable
  prefixed tables. The transfers suite races two free claims and asserts exactly
  one completed ownership transfer/delivery. Production rows are never test data.


### Seamless marketplace refresh

Run `python tests/marketplace-live-ui-preview.py PATH_TO_PHP` and open its local
URL in a browser. Click **Run refresh regression**. The fixture loads the shipped
website scripts with a disposable SQLite catalog/player snapshot and intercepted
local responses. It invokes the actual background timer callback rather than
waiting for each 30-second tick. The PASS result checks twenty unchanged polls
for zero listing DOM mutations, preserved images, viewport, search/caret and
hover state; price/offer changes, additions/removals, list price disclosures,
My Items, offers, wallet history, unsaved setup fields and delayed SC/HC responses
are also checked. Free-only filtering, combined search, refresh persistence and clearing the filter are covered with simulated giveaways. No production requests or real save writes occur. Stop the
reported local PHP PID after checking its executable and localhost port.


Marketplace experience additions:

- `marketplace-bundles-test.php`: member validation and atomic payment/ownership,
  a forced mid-transfer exception/rollback, per-ledger bundle history, one sale
  count, retry/claim cancellation, authorized-write recovery, aggregate bundle
  delivery notification and unbundling. MySQL validation additionally races two
  free-bundle buyers and checks one winner owns every member.
- `marketplace-experience-test.php`: saved property/socket/currency matching,
  baseline and revision deduplication, scoped private searches, hidden-stat
  refusal, notification preferences, muting, message blocking and activity reads.
- `marketplace-refine-test.js`: shared browser refinement math, identified jewel
  additions, conditional set exclusion, scaled/per-level units and bundle members.
- `marketplace-players-http-test.py` additionally verifies filter opt-in, a full
  offline buffer respecting changed preferences, and cursor progress beyond
  more than 100 muted messages without losing newer trade notifications.
- `run-marketplace-resilience.ps1 -ModData PATH -ReferenceSaves PATH` runs the
  isolated domain and real-copy HTTP/save interruption suites together, preserving
  the supplied reference files and writing `build-marketplace/resilience-last.json`.

Loading performance: run `tests\run-loading-performance.cmd <output directory> [D2R.exe]`
from an x64 Visual Studio developer prompt. The test executes the production
upload-limit relay and verifies loading/gameplay limits, draining in-flight jobs,
register/stack preservation, relative branch bounds and Windows unwind behavior.
Supplying the supported game executable also checks the audited native signatures
and unwind parity against its real frame metadata without launching the game.

HUD runtime: `tests\run-hud-runtime.cmd <output directory>` compiles the actual
combat, command-dispatch and monster-overlay modules into a native fixture.
It covers a real message-only Windows window (producer vs window-owner thread),
session/loading cancellation, queue bounds, startup timing, injected Detours
transaction failures, guarded native arguments, cyclic/invalid unit lookups,
combat-text overflow, concurrent render/damage callbacks, and a headless ImGui
overlay with one font and zero maximum HP. No game is started or modified.
`run-floating-damage.cmd` separately exercises the real damage rendering and
configuration implementation, including concurrent toggle changes.

HUD preferences: `tests\run-hud-config.cmd <output directory> [HUDConfig.json]`
exercises the production JSON store and preference/binding models. It checks
real Win32 file replacement, injected write/partial-write/flush failures,
locked and malformed files, external edits, concurrent updates, skipped
unchanged writes, string-safe comments/trailing commas, sparse camera slots,
legacy hotkeys and immutable input-thread snapshots. Optional configurations
are read and round-tripped in memory; the originals are never written.

HUD chat and sounds: `tests\run-hud-chat-sound.cmd <output directory>` builds
the production notification inbox, native identity adapter and sound library.
It covers guarded disabled buffers, concurrent message producers, local-player
and joiner filtering, session/toggle cancellation, cooldown/burst handling,
bounded queues, cached directory lists and selected paths, imports/refreshes,
source precedence and Unicode playback paths. Filesystem and native memory
reads are real; time and audio APIs are stubbed. No audio is played, no external
player is opened, and all fixture files stay in a new test-output subdirectory.

HUD hook setup: `tests\run-hud-hook-setup.cmd <output directory> [D2R.exe]`
checks the production grouped installer against begin/update/each-attach/commit
failures, complete pointer rollback, retry throttling, diagnostic deduplication,
and ready-frame fast paths. Real Detours tests hook/call/restore seven fixture
functions and compare separate versus grouped transaction overhead. An optional
game executable checks all 12 affected native targets in a private image copy;
the EXE is never launched and none of its code is executed.

## Process memory scanner

Run `tests\run-process-pattern-scan.cmd <output directory>` from an x64 Visual
Studio developer prompt. The test uses private allocations and the test process
only. It checks literal `3F` and wildcard handling (including the camera angle
signature), chunk boundaries and end bounds, guard/no-access/execute-only pages,
read failures and partial reads, memory protection changing between query and
read, scratch/signature self-match rejection, concurrent scans, and concurrent
module lookup with reused name buffers. The existing 64 MiB region limit is
preserved. Failed reads discard overlap and continue at the next chunk.

The runner also reports a five-sample median for a 16 MiB fixture, comparing the
legacy inner matcher with the checked scanner including `ReadProcessMemory`.
This measures scanner work, not game startup, camera UI latency, or FPS. The
existing `run-pattern-scan.cmd` separately checks startup signature parity against
a privately mapped D2R executable without executing game code.
## Camera controller

Run `tests\run-camera-controller.cmd <output directory>` from an x64 Visual
Studio developer prompt. The production controller uses one asynchronous scan
task, game/loading epochs, and checked native reads/writes. Tests use private
allocations with real Win32 memory APIs and replace only the native game globals
and signature discovery.

Coverage includes late results across game changes/exits/loading, cancellation
before the second scan, one pending task under repeated requests, scan exceptions
and explicit retries, TCP/IP clients without a local game token, native float
layout and defaults/reset, non-finite inputs/captures, freed/read-only memory,
and cross-page write preflight. Loading tests preserve customized bindings and
original defaults, reject writes during loading, revalidate on resume, and resolve
a replacement camera when an old allocation is gone. Customized angles must not
be rediscovered using only their original default-byte signature. New captures
rearm preset autoload; loading alone preserves the current selection.

The production adapter rechecks game/loading flags immediately before writes.
The fixture also checks 100,000 unused hidden ticks with zero native lookups and
10,000 ready ticks with zero scans/camera reads/writes. Timing is a test-process
observation, not an FPS or game-loading measurement. No live game or saves are
used by this runner.
## Loot-filter engine updater

Run `tests\run-lootfilter-updater.cmd <output directory> [engine.lua]` from an
x64 Visual Studio developer prompt. Tests touch only a new private directory and
loopback HTTP; the optional engine path is read-only. No live author download or
installed engine replacement occurs during this test.

The production module has one owned asynchronous task, immutable request/result
messages, non-executing Lua syntax/metadata checks, overflow-safe numeric version
comparison, bounded body reads, WinHTTP phase timeouts, and a best-effort overall
budget checked between network calls. WinHTTP's timeout notification can be
coarser than its configured millisecond value. The stalled-server test checks a
native timeout before server disconnect and allows timer granularity.

File cases exercise real Unicode paths, ReplaceFile backups, short writes,
write/zero-progress/partial-write/flush/replace/move failures, sharing locks,
external edits/removal/creation during downloads, and temporary cleanup. Existing
backups and unrelated authored configs survive. Network cases cover successful
GET, 404, oversized Content-Length, truncated responses, and stalled reads. Task
cases check one worker under repeated starts, immutable targets, exactly-once
completion, and 100,000 idle polls. Lua source is compiled for syntax without
running downloaded code; this does not prove its eventual runtime behavior.

## Installed loot-filter catalog

Run `tests\run-lootfilter-catalog.cmd <output directory>` from an x64 Visual
Studio developer prompt. This compiles the production asynchronous catalog and
uses private filesystem fixtures. No original filter/config files are written.

Gated workers check nonblocking reads, one task at a time, immutable snapshots,
root changes, invalidation during discovery, failure backoff/recovery, empty
results, clock rollback, and completion timestamps when the dropdown stays
closed. Real directory contention verifies that failed scans keep the last
usable list. Unicode roots/mods/filenames, source precedence, packaged configs,
hidden/support exclusions, imports, external removals, and special tail ordering
are checked through the production discovery/resolution code.

The runner reports a seven-sample median for repeated discovery/sort versus warm
cached lookups with 120 private filter files. It measures list lookup overhead,
not FPS. `run-lootfilter-library.cmd` also accepts an installed game root for
read-only resolution checks against the actual mod package.

## Theme image reloads

Run `tests\run-theme-image.cmd <output directory>` from an x64 Visual Studio
developer prompt with Python on PATH. The runner extracts the production texture
upload, slot mapping, and render adapter verbatim. Only the clock/device source
and image-directory lookup are adapted; uploads use real D3D12 resources through
the private WARP software device, and decoding uses the production stb decoder.

Tests cover all 17 window/frame/checkbox/button slots, descriptor offsets, idle
frames, completion-based retry backoff, device-reference balance, missing/corrupt
images, device acquisition failures, clear cancellation, changed selections,
stale completions, and restoring chosen filenames after renderer reset. Upload
validation covers dimensions/formats/descriptor bounds and preserves existing
textures/handles on failure. The final test deliberately removes only the private
WARP device to exercise a real native allocation failure after preflight.

The 10,000-frame missing-image fixture counts decode attempts and device
acquisitions. It does not measure game FPS. `run-d3d12-reset.cmd` separately checks
the exact production renderer reset against real WARP create/reset/refill cycles.

## Localized controls and translation format validation

Run `tests\run-localized-controls.cmd <output directory>` from an x64 Visual
Studio developer prompt. The runner compares the production linear printf-format
scanner against the previous production regex for generated grammar combinations,
seeded random strings, all byte values, and every shipped source/translation pair.
Two-MiB width/precision cases test bounded-stack scanning without invoking the old
recursive regex on pathological input. Placeholder order, width, precision, length,
conversion, escaped-percent counts, and existing invalid-format behavior stay the same.

Real headless ImGui tests cover array/zero-separated/modern/legacy dropdown sources,
preview-only evaluation while closed, visible-row clipping, getters that reuse one
buffer, failed legacy getters, backwards zero-list traversal, mouse selection, and
stable open popups/IDs across a catalog reload. Fixture timings compare all-pack
format validation and a closed 10,000-entry dropdown; they do not measure FPS.

`run-hud-localization.cmd` separately exercises embedded resources, personal overrides,
editor exports, restart preferences and full multilingual font atlases in private
LOCALAPPDATA. `run-hud-runtime.cmd` covers the existing combat/command/overlay runtime.

## Translation catalog publication

Run `tests\run-translation-store.cmd <output directory>` from an x64 Visual
Studio developer prompt. The production store freezes catalog copies, reuses
identical historical contents, and publishes an atomic pointer for text lookups.
Old snapshots remain alive for the store lifetime so existing UI text pointers
stay valid. Distinct edited versions are retained; this is deduplication, not
reclamation of text pointers that callers might still hold.

Tests cover repeated unchanged full-pack reloads, returning to earlier contents,
mutable source aliases, null/fallback publication, rejected-entry metadata,
deliberately colliding fingerprints, publication exceptions and recovery,
non-terminated string views, UTF-8, and concurrent readers/writers. Retention is
checked against the previous append-only publication behavior. Seven-sample
lookup timings compare the previous atomic shared-pointer read with the new
immutable store; these are component measurements, not an FPS benchmark.

The localization, localized-controls, and HUD runtime runners provide integration
coverage for file reloads, retained arguments, widgets, glyph collection and fonts.

## HUD command delivery

Run `tests\run-hud-commands.cmd <output directory>` from an x64 Visual Studio
developer prompt. The runner compiles the production dispatcher and session
tracker. Real User32 delivery targets private message-only windows; game command
entry points and native globals are fixtures. No real game/network commands run.

Coverage includes zero-host-token TCP/IP joiner contexts, startup delay, observed
exit/reentry with a reused native token, changed game/window/configuration,
loading before/after posting, shutdown cancellation, concurrent producers,
reentrant native callbacks, bounded FIFO backpressure and wrong-thread rejection.
Injected registration/post failures check retry backoff, completion timestamps,
clock rollback, retained delivery and recovery. Slash commands keep the native
client entry point; Tab/Alt use window-local key messages.

An unrelated-message fixture performs 100,000 handler calls with zero native
context reads/registration calls. A 10,000-poll failure fixture advances its clock
one millisecond per poll and counts 100 post attempts. These are counted work
fixtures, not FPS measurements or live TCP/IP validation. Existing HUD runtime
and chat/sound suites provide integration coverage for the opaque session token.

## Rule-builder catalog loading and choices

Run `tests\run-lootfilter-builder-catalog.cmd <output directory>` from an x64
Visual Studio developer prompt. It compiles the production loader with injected
asset reads, time and worker launch failures, and also exercises the default
`std::async` worker with a blocked reader. Render-side polling must return while
that reader is blocked, and must not launch duplicate readers.

Coverage includes completion-based one-second retry delays, retained catalog
data after failed reloads, worker/future/launch failures, invalid futures, clock
rollback, source changes and repeated invalidations. Old work cannot publish
into a different source generation. Successful catalogs remain cached; partial
catalogs explicitly marked loaded retain the existing degraded behavior.

Choice-list tests verify borrowed catalog storage, shared fixed choices and a
separate small list for All items and unknown selections. The 5,000-item fixture
performs 10,000 borrowed list reads. Its timing is a component fixture and does
not measure FPS. A separate allocation counter compares 100 copies of that
catalog with 100 borrowed reads: 500,100 vector/string allocations versus zero.
`run-lootfilter.cmd` adds actual ImGui popup interaction for
unknown-selection removal and All items/specific-item switching, plus existing
editor, preview, simulator, Lua engine and load/save regression coverage.
`run-catalog-bin.cmd` checks the installed compiled tables without editing them.

The production loader is retained until process exit, avoiding future teardown
under the Windows loader lock. It is render-thread owned; background work owns
only its input values and result, and never calls ImGui or accesses game units.

## Shared background jobs and item artwork

`d2rhud/BackgroundJob.h` owns one in-flight operation, polls without waiting,
contains launch/future failures and carries the caller's generation ticket even
when the worker throws. It refuses deferred futures so asset work cannot silently
run inside a render-thread poll. Installed-filter discovery, rule-builder catalog
loading and quantity-popup artwork all use this component. Cache policy and
retry timing remain with each feature; it is not a global worker pool.

Run `tests\run-item-sprite-cache.cmd <output directory>` from an x64 Visual Studio
developer prompt. It covers the shared job contract, one-worker ownership,
source changes, exception recovery, completion-based two-second retries,
clock rollback, normalized/unsafe icon paths, LRU eviction, missing/empty/oversized
results and retained caller-owned pixels. A 10,000-request alternating-icon
fixture needs two reads/decodes. It measures CPU cache work, not FPS or GPU uploads.

The production `StackSplitSkin.cpp` wrapper is also compiled into the fixture.
GameAssets reads and native mod memory are private test data, while actual sprite
decoding and async scheduling run. A gated startup reader confirms that panels
and mappings stay private until complete publication; a replacement keeps old
shared snapshots usable and invalidates cached icons only after publication.

Runtime cache bounds are eight entries and 32 MiB of retained pixel capacity.
Active worker results and shared images retained by callers/GPU code are outside
that cache budget. Production state is process-resident. A child-process fixture
compares normal static-future teardown (waiting for a two-second worker) with
retained job state (no exit-time join). The existing process-lifetime runner
separately covers explicit graphics reset and COM teardown behavior.

Re-run `run-lootfilter-builder-catalog.cmd` and `run-lootfilter-catalog.cmd` after
shared-job changes. `run-stack-split-ui.cmd` exercises real quantity controls and
native artwork at 720p, 1080p and 4K; it also verifies four-digit entry and submit
behavior. None of these fixtures controls a real character or edits saves.

## Frame snapshots, deferred feature assets and presentation boundaries

`FrameContext.h` samples main-image game/loading/session flags once for the
render frame. The HUD, command scheduler and camera tick consume that value.
Queued command dispatch and camera writes still sample fresh flags immediately
before native actions. `run-hud-commands.cmd` and `run-camera-controller.cmd`
check zero additional native lookups across 10,000 supplied-snapshot ticks and
reject actions when loading, exit or a session change follows an earlier sample.

`DeferredAssets.h` builds on the shared background job. Buff icons and stat
ranges request data only when enabled, configured for hotkey use, or previewed.
Work already requested may complete while inactive. Source changes and manual
reloads reject stale results; launch/future failures back off and recover.
Snapshots are immutable and runtime loader owners are process-resident.
Table readers retain their existing partial-data/error behavior; buff settings
keep their manual retry action.

Run `tests\run-deferred-assets.cmd <output>` for the generic lifecycle. The
100,000-tick inactive fixture launches zero workers and performs zero asset
reads. Existing `run-buff-icons.cmd` and `run-stat-ranges.cmd` compile the real
features and check 10,000 disabled updates without workers. The stat-range test
also counts zero foreground-window lookups while inactive. Optional installed
assets exercise actual TXT/BIN combinations, skill/state/icon links, property
records, native tooltip signatures and layout/scaling/configuration behavior.

Camera presentation lives in `CameraEditor.cpp`; preference windows and status
messages live in `HudPresentation.cpp`. Their runtime host passes explicit
settings and callbacks rather than exposing main-HUD globals. The presentation
code owns no configuration files or native game memory reads/writes.
Run `tests\run-camera-editor.cmd <output>` for real ImGui drawing and private
camera memory. It covers saved presets, resets, autoload, viewport sizes, idle
preference rendering, edit/save/failure/retry/close transitions, notification
expiry and balanced font stacks. These component counts and headless checks
do not establish overall FPS or live-game interaction behavior.

Reported gameplay/startup regressions: `run-d3-whirlwind.cmd <output> <D2R.exe>`
checks zero-life channel termination, belt eligibility, and the real native
pickup handler at customized distances. `run-terror-zone-rotation.cmd <output>`
covers manual cycling across scheduled boundaries and active/break countdowns.
`run-cube-map.cmd <output> <D2R.exe> <excel directory>` includes the ten-second
clear delay, frame wraparound, late monsters, and Soul Surprise ground loot.
`run-item-text.cmd <output>` checks 4,096 bounded multilingual copies with
guard bytes. Pandemonium's separate Core tests accept `--localization-startup`
to verify catalog-derived early allocation, executable guards and rollback.

## Session cleanup and local diagnostics

`run-session-telemetry.cmd <output>` covers shared session generations, loading
transitions, character switches, item cache cleanup, disabled counter overhead,
threaded samples and reset isolation. `run-runtime-diagnostics.cmd <output>`
checks cached fault identity, bounded support reports, preference preservation,
stale tooltip publication and the real ImGui diagnostics panel.

The Setup > Diagnostics page saves a local support report. Timing counters are
off by default; enabled counters report inclusive wall time, so nested rows
overlap. Neither component tests nor startup timings establish gameplay FPS.
Pandemonium Core tests also cover launcher reports for failures before HUD load.

## Optional Run Tracker

`run-run-tracker.cmd <output>` exercises deterministic town/loading/manual pause
timers, session boundaries, manual splits, deaths, retained inventory and stack
deltas, transfers into later-loaded stash pages, discards and consumption. It
checks versioned run-file round trips, malformed/future-version preservation,
interrupted recovery, statistics and CSV escaping. The UI fixture renders all
five sections and item-analysis subviews at 720p, 1080p and 4K and simulates timer dragging, locking and saved
position. Ten thousand disabled ticks invoke no native sampling.

The separate native fixture uses private memory for character paths, inventory
chains, shared stash pages and the client item hash. It verifies ownership,
stack quantities, name-cache/session boundaries, read guards, cycle/loading/page
switch rejection and exclusion of ground items. These checks do not replace an
in-game inventory/stash smoke test. Run files are under
`%LOCALAPPDATA%\D2RHUD\RunTracker`; no character or stash save files are edited.

`run-run-tracker-analysis.cmd <output> [D2R install]` checks composition shares,
empty-yield areas, pricing coverage, medians, currency isolation, SC/HC and mod
isolation, stack normalization, stale read-only history, excluded sale records,
TC weighting and unsupported paths, analysis caps and old journal compatibility.
It exercises the real TXT/BIN asset loader against private fixtures and optionally
the installed RMD-MP catalog. A 100,000-entry calculation reports its elapsed
time; this is an analytics workload measurement, not a gameplay FPS benchmark.

The tracker fixture also tests confirmed data-clearing behavior on private files:
all loaded/older/malformed run records, optional CSV removal, preference and
unrelated-file preservation, locked-file errors/retries, active-run rebaselining,
and stale load/save/retry completions that must not resurrect erased history.
It renders the confirmation modal with its destructive action disabled until
acknowledged. No player history is cleared by these tests.

## Shared user preferences

`run-user-preferences.cmd <output>` tests migration of all five legacy preference
files into namespaced `preferences.json`, exact original-byte recovery in one
archive, idempotence and shared-section precedence, profile isolation and pending
Controls edits, layout restore/save through real ImGui, malformed/future/locked
file refusal, damaged-section isolation, interrupted migration and concurrent
section writes. Fixtures use private directories and a private LOCALAPPDATA.
The Controls, localization, diagnostics, window layout, Run Tracker and base
HudConfig suites also exercise their consumers of the shared store.

## Inspect

`run-inspect.cmd <output> [D2R.exe]` exercises the real client/host protocol with private
native-memory fixtures, including equipment, backpack positions and charm stats,
player GUID zero, monequip-generated companion inventory, deferred server reads,
fragmented packets, stale sessions and malformed payloads. Stash and trade items
are excluded and no inventory writes occur.
It also covers native formatted item text, invalid-target and town-exit handling,
automatic loading before the host handshake, and clients whose unused struct tail
is unreadable. Passing the game executable additionally verifies camera/accessor
signatures and tests custom summon ownership, town restrictions, projection and
native menu occlusion. A captured town camera/flag fixture covers always-visible
HUD elements and top-edge icon clipping. Client and host fixtures also enforce
the loaded MonStats inventory flag, including empty equipment-capable units. The cursor fixture verifies town-only
ally selection using native sprite bounds, without changing combat callers or unit flags.

`run-inspect-ui.cmd <output> <game-directory>` reads the installed mod's inventory
layout, background and HD item mappings without changing game files. It checks
720p, 1080p, ultrawide and 4K bounds, plus charm tooltips and long unit names, and
captures ImGui draw data for visual review. It requires CascLib headers/libraries.
The panel uses the native inventory scale, original close/weapon-tab artwork and
centered, colored inventory tooltips. The eye-icon fixture also checks clustered
icons, optional readable labels, large-font edge placement, actual ImGui click
selection and mouse pass-through outside the icons.
No game window is opened by these tests.
The graphics reset suite also verifies cleanup of Inspect textures and fences.


`run-spectate.cmd <output> [D2R.exe]` covers native presence/consent, protocol
revision, session and generation changes, fullscreen input, real-executable
signatures (including the detoured queue), per-client random socket grants,
TCP/IP session kinds and captured native party geometry. Its WARP test runs
actual 4K GPU capture through persistent H.264, the authenticated TCP host relay,
decoding and GPU display, including 1080p/4K changes, color, texture allocation
reuse, reset and idle. Offscreen UI checks cover 720p/1080p/ultrawide/4K, exact
party eye hitboxes, PiP/fullscreen and settings/layout persistence.

`run-spectate-h264.cmd <output>` checks codec buffering, 60 complete 1080p
encode/decode frames, inter/keyframes, color, live resizing and malformed data.
`run-spectate-stream.cmd <output>` runs independent real socket clients with a
host relay, forged/stale credentials, opt-in privacy, replay guards, live profile
changes, reconnects and disconnects. It measures a paced 120-frame 1080p30
textured/moving synthetic scene end to end, including encode/decode, bandwidth
and latency, against the previous JPEG encoder retained only in the test tree.
These are synthetic local measurements, not remote gameplay guarantees.
No tests send desktop input, start D2R, or write another process's memory.
