# Tests

Run the Windows C++ fixtures from an **x64 Native Tools Command Prompt for
Visual Studio 2022**. Restore the dependencies in
[../d2rhud/vcpkg.json](../d2rhud/vcpkg.json) first. Use a temporary directory for
compiled test output; build products and caches are ignored by Git.

## Examples

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
