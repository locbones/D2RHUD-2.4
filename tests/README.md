# Compatibility tests

## Loot filter builder

`run-lootfilter.cmd` also runs `lootfilter-builder-test.cpp`. The optional fourth
argument supplies the actual `lootfilter.lua` engine for semantic tests of
generated rules and every advertised effect preset. Tests cover custom base,
type, stat, area and named-item catalogs; numeric/stat comparisons; escaped text;
color/action serialization; legacy aliases; field removal; and preservation of
comments, expressions, translations and generated code. The editor test selects
rules with mouse events and draws the guided controls at 960p, 1080p and 4K.
Its optional output-directory argument exports draw data for visual checks.
The optional second argument supplies the actual engine for preview captures.
`lootfilter-preview-test.cpp` also runs the installed engine in the preview
adapter: match/nonmatch, hidden and mismatch-hidden states, appearance, every
effect preset, inherited/custom categories, threshold examples, translated
text/styles, and notification collection. It checks authored-code rejection,
filesystem/audio isolation, and memory/instruction limits. Preview results never
replace the game's active filter.

The D3 Whirlwind suite also tests native automatic gold pickup while channeling.
With an executable argument, its core fixture runs the real AutoGoldManager
update in a private image to verify option/capacity/delay/cursor gates. The hook
fixture verifies all nine detours install and restore, including the scoped
native interaction hook. See [Whirlwind verification](../docs/d3-whirlwind.md).

## Terror-zone rotation

Run `tests\run-terror-zone-rotation.cmd <output-directory>` from an x64 Visual
Studio developer prompt. Tests cover hourly and terror/break boundaries,
wraparound, random offsets, invalid periods/groups and extreme UTC timestamps.
Both automatic selectors call the same helper; their manual-selection branches
are unchanged. See [rotation integration](../docs/terror-zone-rotation.md).

## Native four-character HD item codes

```bat
tests\run-item-codes.cmd C:\temp\d2rhud-item-codes-tests "C:\Games\D2R" RMD-MP
```

Run in an x64 VS developer prompt. No vcpkg libraries are needed. The game/mod
arguments are optional; the mod argument reads loose `data/hd/items` JSON files.
Tests execute only HUD-owned replacement instructions in a private test buffer.
The optional game executable is parsed as data, never loaded or executed.

Coverage includes original short-key compatibility, fourth-character/prefix
collisions, case and all 255 nonzero byte values in each position, bounded reads
at guard pages, full loader/base/tier-selection signatures, altered code rejection,
late-load rejection, patch boundaries, idempotent installation, guarded restoration
and unchanged surrounding code. Local RMD-MP checks read 1,727 base, 199 set and
677 unique entries; all 93 four-character base mappings resolve independently,
including 83 collisions in the original three-character encoder. Mod files are
never rewritten by the tests.

In-game verification: restart with HUD loaded at startup; check native status in
HUD settings. Test base items with the same first three characters and distinct
fourth characters, plus a matching three-character code. Verify inventory, ground,
equipped and character-select visuals, identified/unidentified set and unique
items, and normal/exceptional/elite upgrades. Turn Transmog Visuals off and repeat;
then verify an explicit transmog still overrides visuals when enabled. Confirm
that existing `transmog_table.txt` is unchanged. Repeat with legacy items and
another mod. Native set/unique JSON entry names stay unchanged.

## General compatibility suite

Run from an **x64 Native Tools Command Prompt for Visual Studio 2022** with the
`x64-windows-static` vcpkg dependencies installed (Detours 4.0.1 and MinHook 1.3.4):

```bat
tests\run.cmd C:\deps\x64-windows-static C:\temp\d2rhud-tests
```

The first argument is the directory containing `include` and `lib`; the second
is an explicit output directory outside the source tree. The runner builds with
`/MT`, leaves assertions enabled, stops at the first failure, and keeps generated
executables and object files in that output directory.

- `queue-test.cpp`: direct and adjusted queue pointers, ambiguous samples,
  mismatched vtables, unreadable memory, arithmetic overflow, and LastError
  preservation. Also exercises real MinHook/Detours chains in both orders and
  verifies callback counts, forwarding and detach behavior.

These tests do not load D2R or validate CrossOver/D3DMetal execution. Run the
queue harness on native Windows; its host-detection assertion expects that host.

## Buff icons

With the project's vcpkg dependencies (including CascLib) installed, run from
an x64 Native Tools Command Prompt:

```bat
tests\run-buff-icons.cmd C:\deps\x64-windows-static C:\temp\d2rhud-buff-tests
tests\run-buff-icons.cmd C:\deps\x64-windows-static C:\temp\d2rhud-buff-tests "C:\Games\D2R" RMD-MP
```

The optional installation also exercises real source/compiled buff table parity
in all eight TXT/BIN combinations, mod BIN versus loose TXT precedence, damaged
tables, and mixed-table link requirements using private copies. Its loose
`data/global/excel` folder must contain matching source and compiled tables.

Checks absolute server expiration, relative client duration, pause/recast,
expiration and early state removal, unreadable/cyclic lists, configuration
validation/round trips, and RGBA/BC3 sprite headers. The optional game directory
also verifies actual data loading and Shout's icon through `_profilehd.json`.
It reads local files and CASC archives without extracting or modifying them.

The skill lookup regression reproduces learned level-20 Warmth and Energy Shield
with the native bonus reader unavailable. It also verifies the real function's
leading REX instruction prefix, which must not cause all skills to be rejected.

Layout checks cover exact single-icon and wrapped-row bounds at 1080p and 4K,
including themes with a large minimum window size, untimed effects, inside labels,
and all timer placements/formats. Label/timer appearance settings are validated
and round-trip through JSON.

In-game checks still required on D2R 1.4.71510:

- Enable **Buff Icons** in the control center, select character skills, and
  confirm that icons appear only while those states are active.
- Recast Shout/armor buffs, pause/resume, remove an effect early, die, and
  leave/rejoin. Compare countdown disappearance with the actual effect.
- Prebuff with an item-granted skill, switch weapons, and confirm that the
  selected active buff remains visible until its state expires.
- Check both TCP/IP host and joining client. The local player's GUID must
  determine the display; client duration fields are relative simulation frames.
- Move the preview, change scale/columns, close/reopen settings, switch
  characters, and resize the game to verify saved choices and GPU atlas reload.

## Stat ranges and extended tooltip text

```bat
tests\run-stat-ranges.cmd C:\deps\x64-windows-static C:\temp\d2rhud-stat-tests "C:\Games\D2R" RMD-MP
```

Checks unique/set and overlapping affix contributions, automagic, grouped stats,
enhanced damage, skill parameters, localized strings, fixed/negative rolls,
runeword socket order/type matching, toggle/hold/repeat/focus transitions, malformed
settings and JSON round trips. Synthetic native glyph/row memory verifies range
placement and rejects invalid counts. ImGui lifecycle checks cover 1080p/4K.
Background fitting is checked before the first native draw at 1080p/4K, with
16/40 px text, 0/40 px spacing, right/bottom screen edges, vertical padding,
native text dimensions, secondary panel translation, repeated frames, no-match
and disabled cases. Published character widths are compared to ImGui's actual
font measurements; the native layout destructor signature is also verified.
Custom-format coverage includes minimum/maximum order, negative/fixed/64-bit
bounds, literal percent signs, malformed/unsafe format specifiers, control bytes,
length limits, JSON round trips/defaults, compound damage and conditional set
bonus bounds, ambiguity before formatting, cached live format changes, and
background fitting with longer labels. UTF-8 label widths are compared against
the actual ImGui font at both supported resolution scales.
Activation integration checks verify that saved always-on mode and latched
hotkey toggles remain visible with HUD menus/text fields open. Hotkey-mode live
preview does not latch or enable hold after closing its settings panel. Hotkey
input stays suppressed and held keys do not fire across menu close. Leaving the
game or losing focus still hides ranges.
Optional game/mod arguments exercise the actual tables, including RMD-MP Rakescar.

Inventory-tooltip regressions run the native builder adapter, generic panel,
text draw and glyph-layout hooks together with mocked native calls. They cover
null/unrelated world hover, enabling ranges after text was built, item/GUID/seed
validation, identical text with differing property bounds, unidentified items,
and ground-item fallback. The optional game directory also verifies the full
native item-tooltip builder code hash against `D2R.exe`, rejecting altered code.
RMD-MP fixtures include the observed Endlesshail damage/cold-resistance/pierce
lines, including a positive stored pierce roll whose description supplies `-`.

Extended-tooltip checks restore full and partially truncated description tails,
keep item identities separate, restore panel state after placement adjustments,
and verify all ten native capacity patches and their fail-closed build guard in
a dedicated synthetic executable allocation. No native code from that allocation
is executed and no running game is modified by the tests.

In-game checks required on D2R 1.4.71510 HD:

- Hover a unique, a set item, and a rare with overlapping affixes. Compare ranges
  with the corresponding mod table rows, including socketed jewels/gems.
- Check set bonus lines, class/individual skills, alternate languages, and long
  descriptions. Unsupported/ambiguous lines should remain unannotated.
- Assign **Show Stat Ranges** in Hotkey Controls. Verify always on/off, press to
  toggle without autorepeat, hold/release including modifiers, focus changes,
  menu/key capture suppression, and leaving/rejoining a game.
- Check tooltip alignment at each screen edge, both tooltip directions, font
  color/size/spacing, and 4K scaling.
- Use a description longer than 1,024 bytes. Confirm that its remaining text and
  background height appear without truncation or stale text on the next item.

## Linear sunder and dynamic native immunity labels

```bat
tests\run-sunder.cmd C:\deps\x64-windows-static C:\temp\d2rhud-sunder-tests "C:\Games\D2R\D2R.exe"
```

Ports the regression scenarios from `D2RHUD-dynamic-immunity-labels.zip` to the
compiled source implementation. Tests cover reductions below 100, exact 99/100
boundaries, repeated refreshes, equipment changes/removal, new UMod resistance
bonuses, all six damage types, per-element multiplayer maxima, configurable
floors, overflow guards, and 2,700 deterministic mixed-element refreshes.

Native-label tests mock game stat access/server lookup and verify server
selection, first-element refresh, stale client resistance, immunity returning,
missing/mismatched server units, no-game fallback, and passthrough for unrelated
stat-reader callers. Every byte of the audited native label sequence is checked;
the optional executable argument verifies both hook signatures against the
installed PE file without loading or modifying it.

Live verification still required: enable `SunderedMonUMods`, leave the floor at
99, and compare damage/hover resistance before and after equipment changes on a
fresh monster. Check immunity labels disappearing at 99 and returning at 100,
UMod monsters, and host/joiner behavior. The tests do not emulate networking or
claim an in-game validation run.

## Extended automap layers

```bat
tests\run-automap-layers.cmd C:\deps\x64-windows-static C:\temp\d2rhud-automap-tests "C:\Games\D2R\D2R.exe" "C:\test-fixtures\maps"
```

The executable and existing-map directory arguments are optional. The executable
is parsed as a PE file without loading its code. Existing `.ma0`-`.ma3` files are
only read; compatibility appends and reloads operate on memory copies.

Coverage includes layers 99/100/101, 255/256, 65,535/65,536, sparse million-scale
IDs and INT32_MAX; four independent cell groups; negative coordinates; separate
layers and repeated exploration batches; 1,000 mixed saves interleaved with a
simulation of the unchanged vanilla writer; malformed/truncated records; invalid
IDs, counts and cache sizes; and cyclic linked-list pointers. Mock native cache,
allocator and cell insertion bindings exercise both hook handlers, low-layer
passthrough, dirty flags, saved-cell flags, sprite bounds and failure handling.
Full code hashes for six audited native functions are checked against the
installed executable and deliberate code mutations are rejected.

Read-only local compatibility check: 137 existing map files and 975 native records
parsed successfully. Every file accepted an extended record in memory without
changing its original bytes. This is not an in-game save/rejoin test.

Native audit (D2R 1.4.71510 RVAs): `0x96060` maintains a dynamic linked list keyed
by a 32-bit layer ID; `0x94a40` and `0x95640` save/load through a 400-byte stack
table indexed by that ID. `0x95530` repairs that table only from the native loader,
which is retained solely for layers 0-99. The file contains consecutive 32-byte
record headers (next/layer/special-sprite-set/metadata/four byte counts), followed
by four sets of 6-byte image/x/y cells. The extended path scans record boundaries
by ID and appends native records without indexing the fixed table. It preserves
low-layer chains and uses the native seed/difficulty resolver (`0x946a0`), resize
allocator (`0x10ed40`), insertion routine (`0x94310`) and cache SRW lock/dirty flag.

Live verification still required on a test mod: assign different layers 100, 101
and 1000, explore only part of each area, move between them and a vanilla area,
save/exit, restart and rejoin the same seed. Confirm that each area's explored
and unexplored portions remain independent, newly explored cells persist on a
second save, and vanilla maps remain intact. Repeat with a changed seed, another
difficulty, legacy/HD graphics and TCP/IP host/joiner. Two levels deliberately
sharing the same layer should continue to share exploration.

## Loot filter engine/editor compatibility

```bat
tests\run-lootfilter.cmd C:\deps\x64-windows-static C:\temp\d2rhud-filter-tests C:\source\D2RLAN-Filters C:\source\D2RLAN-Filters\lootfilter.lua
```

The optional third argument scans the author/template corpus without modifying
it. The fourth exercises the real transactional loader against a temporary copy
of the engine. All test writes stay inside test-created temporary directories.

Coverage includes condensed/multiline tables; single/double quotes; escapes;
long strings/comments; braces and comment markers in text; keyed field names;
semicolons; BOM/CRLF; nested functions and stat groups; named returned tables;
source-preserving edits around generated loops; zero-rule deletion; long rules;
byte-identical no-op saves; Lua syntax rejection; backups and external-file
conflicts. The production editor load/save code and all five tabs are exercised
at 960px, 1080p and 4K, including real ImGui keyboard input growing to 24 KB.
Transactional loader tests verify private environments, runtime diagnostics,
strict/syntax failures retaining working callbacks, recovery and fresh overrides.

Run the engine's semantic/native-mock corpus separately with
`D2RLAN-Filters/tests/lootfilter-test.lua`. On October 16, 2026, all 3,333 published
author/template rules compiled; an additional 230 rules from the installed
Pandemonium config also compiled without disabled rules.

In-game verification remains required: apply one retail and one ReMoDDeD config;
check ground/dropping/inventory/equipped/vendor items; compare notifications,
TTS, localized text, borders and animated presets; cycle levels on a condensed
config; edit/reorder/delete a rule; verify generated-source edits and a failed
strict reload; check hidden-item toggling, TCP/IP host/joiner and tooltip order.
The tests do not launch D2R or claim an in-game run.

### Legacy item transmogrify

From a VS x64 developer command prompt, run:

```cmd
tests\run-legacy-transmog.cmd "%TEMP%\d2rhud-transmog-tests" "C:\path\to\D2R.exe"
```

This exercises the production client/server adapters against owned mock units:
Z02/Z03 round trips, creation descriptor layout and parameters, quantities,
original-slot/free-slot/ground placement, allocation and removal failures,
restoration and cursor recovery, invalid bounds/targets, ownership and page/mode
checks, malformed use commands, local-server gating, unrelated native items and
full four-byte target lookup. Client eligibility is also tested with the observed
Z02 client flags `0xa00011`
and server flags `0xa80010`: the client NEWITEM bit must not prevent sending the
use command, while pending transaction flags remain rejected on the server.
The optional executable is read only. Every guarded native routine is verified
against the file; mutating any signature is rejected.
MinHook installation, trampolines and clean removal are checked inside a private
mapped copy of that executable; no live game is patched by the test.
The production remove/add adapters also execute the real native wrapper routines
inside that private image, with world services replaced by test stubs. This
reproduces the update-list/removal-list ABI error and checks actual detachment,
placement, and the guard against a successful return that retains ownership.
The real native notification wrapper and dispatcher also run in that private
image, with the final transport captured. They must emit removal before addition,
serialize the replacement as a new item, and restore temporary flags afterward.
Tooltip checks cover destination localization, both conversion directions,
invalid names/targets, remote-game suppression, matching explicit control greys,
preservation of name colours with a trailing grey reset, empty control footers, and the footer's reversed
line order placing the hint below the drop control on screen. Hook lifecycle
checks verify that the reversed description builder stays untouched. The real
native localization and string-append routines also execute in
the private image, checking lookup fallbacks and the updated layout text length.

Live verification on October 16, 2026 confirmed repeated Z02/Z03 conversion in
RMD-MP, with the final client and server inventory item ID and base class matching.
The running process remained responsive after eight use commands.

### Cube visual outputs

Run `tests\run-cube-visual.cmd "%TEMP%\d2rhud-cube-visual-tests" "C:\path\to\D2R.exe"`
from a VS x64 developer command prompt. Tests cover both aliases, quoted names,
four-character codes, base/unique/set resolution, stable cosmetic IDs, exact
preservation of source item bytes and pointers, signed save/send round trips,
placement rollback, modifier conflicts, retained recipe level columns (including
the reported `lvl=125` recipe), and invalid stat configuration. An owned
executable image checks all native signatures and hook lifecycle, and executes
the actual parser and linker for base, unique and set targets. The set-name
branch's zero return despite a valid output record is explicitly exercised.
The reported hand-axe recipe is also tested with `vis=Griswold's Edge` present
only in TXT while the already compiled output contains plain `useitem`. Recovery
must apply the visual without changing the shared recipe, and mismatched source
row/input counts must stop the recipe before consuming ingredients.
Visual output notifications must request full item serialization; ordinary moves
outside a visual recipe keep their original notification flags.
The native stat setter, modifier-list attachment, and complete item serializer
also run in an owned executable image. This reproduces the missing selector when
it is stored in unit base stats, then verifies that placing it in the state-zero
MAGIC modifier list serializes stat 435 and its biased negative value. A second
selection verifies replacement. Fixtures also cover creating the saved modifier
list for an ordinary item and rejecting unavailable stat lists before consumption.

Live checks: use `useitem,vis=<basecode>`, then a unique/set `index` name; compare
all item stats, sockets, charges, durability and inventory graphic before/after,
equip it, change areas, and save/rejoin. Confirm appearance refreshes without a
stat reroll and that older positive transmog selections still work. Do this on a
backed-up test character. These live checks are pending.
The hand-axe to Griswold's Edge recipe has been confirmed visually in-game;
client/server stat 435 both held -4133 and the world-model lookup ran three times.
The remaining variations and save/rejoin checks still need live coverage.

### Extended states

Run `tests\run-states-512.cmd "%TEMP%\d2rhud-states-512-tests" "C:\path\to\D2R.exe"`
from a VS x64 developer command prompt. The optional executable is mapped into
private owned memory; no running game is patched. Tests cover packet boundaries
254/255/256/511, malformed framing, signed stat values and layers, native receiver
callbacks and visual cleanup, registered x64 unwind metadata, native state-511
activation/removal without overwriting adjacent callback pointers, 512-state full
synchronization, delta updates, curse removal, legacy delegation, signature
rejection and installation failure rollback. The loader's actual comparison is
executed before and after patching: 512 rows are accepted and 513 rejected.

`generate-states-512-native.py` regenerates receiver relocation/unwind metadata
from the audited executable using Python `pefile` and `capstone`. It accepts the
executable path and optional output header path, checks its SHA-256, and does not
copy executable code into the repository; runtime copies come from the verified
game image.

Run `tests\run-states-startup.cmd "%TEMP%\d2rhud-states-startup-tests" "C:\path\to\D2R.exe" "C:\path\to\d2rhud.dll"`
for the startup handoff regression. It detects the HUD readiness export without
executing the DLL, rejects a changed native loader, holds an owned thread at the
real loader comparison, restores its instruction before HUD verification, checks
readiness rejection/timeouts, and resumes a 259-row comparison after patching.
The live failure was an initialization race: the old helper's grace period let
259 rows hit the original assertion before HUD patched it. The updated helper's
native gate loaded the same expanded table successfully in an observed startup.
An isolated test mod also reached 512 compiled rows and remained alive after
resuming with the verified 513 comparison. Neither startup test loaded a
character. The temporary mod directories were then archived outside the game's
Mods folder, leaving RMD-MP's 259-row table intact.
The user then confirmed reaching character selection through the installed
Pandemonium helper. A read-only live check showed 259 compiled rows, comparison
513, installed state hooks, a responsive process, 21 full synchronizations and
3 delta synchronizations with no rejected updates. No extended-state packets
had been sent yet, so high-ID gameplay remains a separate pending check.

Live validation: fully exit D2R, use the updated startup helper, expand a test mod to 512 state rows while keeping
existing IDs stable, and start with HUD loaded before tables. Exercise a skill
using IDs 255, 256 and 511, including expiration, recasting, dispelling/curse
removal, area transitions and rejoining. Check stat effects and client visuals,
then test host/joiner synchronization with matching HUD and tables. Do not change
the state row count through Reload Tables with a character loaded. These live
expanded-table checks remain pending.

Further in-game validation: enable Z02 -> Z03 and Z03 -> Z02 with bounds 1/1; right-click
both ways in personal inventory, then save/rejoin and verify the result persists.
Also test a stack target with fixed 200/200 and random 100/201, a larger target
with room elsewhere, a full inventory that forces a nearby drop, a four-character
target and an invalid target that retains the source. Compare ordinary right-click
items (potions, scrolls and cube). Run these checks in both HD and legacy graphics.
# Cube map recipes

The Survivors reference is the supplied executable with SHA-256
`e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8`.
`python tests/generate-survivors-native.py <D2R.exe>` regenerates its 37 entry
checks without modifying the executable. The earlier `2c2ddb60...f652360`
variant produces the same signature rows and remains accepted by this generator.

Signature-failure fixtures also verify exact pre-hook byte capture, failed reads,
common x64 direct/indirect entry jumps, cycle limits and destination-module
reporting. Installation retains that snapshot for `feature-status.txt` even
after other HUD hooks are installed. Diagnostics do not relax compatibility checks.

The save-limit regression applies all eight launcher edits for 32 KiB, verifies
installation and teardown preserve them, and rejects each missing edit, an
unrelated body change and an unaudited size. Both variants check the complete
599-byte native save-dispatch function. A mapped-native fixture fills/transmits
both complete 8-KiB and 32-KiB buffers through the actual stack probe, cookie
check and register restoration, with only serialization/logging/network calls
mocked. Existing avatar fixtures continue to verify normal-hero save isolation.

Run `tests\run-cube-map.cmd <output-directory> <D2R.exe>` from an x64 Visual
Studio developer prompt. It checks parsing and source recovery, refusal of
occupied destinations before removal, room reload ordering and loot
preservation, monster death/resurrection tracking, packet bounds and order,
native signature guards and hook lifecycle. A private executable image also
runs the actual room-removal and preset-spawn wrappers, cross-act warp branch,
and temporary stat-list attachment/removal with a state above 255.

The cube visual suite also verifies map preparation before ingredient
consumption and rejection of mixed map outputs. Full in-game room generation,
portal traversal and multiplayer completion still require gameplay testing.

### Cube map improvements

Random-destination tests cover `map=zzz`, all trial/Icy Hell keys, forbidden
realms, valid custom areas, per-difficulty population/geometry checks and source
recovery. The cube adapter checks resolution per invocation and keeps inputs on
failure. Elite tests count named leaders and champion groups once, validate all
six pack budgets, restore scoped settings and refuse partial/blocked groups;
native fixtures exercise champion siblings and the ordinary-population branch.
Modifier tests cover the full pool, paired-stat attachment/removal, conflicts
and missing stats. Native TC tests execute difficulty upgrades, positive and
negative Picks, NoDrop RNG, recursion, expanded caps and bounded caller buffers
without modifying treasure-class records.

Layout regression tests cover cached-room retirement and execute the native
second-entry status propagation with poisoned freed room headers. Jungle tests
check connected river/clearing paths, preserved boundary exits and head/tail
rows, deterministic client/server results, ordinary-template restoration and
different native Flayer Jungle preset emissions on successive rerolls. Layout
protocol version 3 rejects clients using the older template generation rules.

`run-cube-map.cmd` covers bounded completion packets, replay suppression, roster exclusions, per-participant reward grants, and real native stat-point persistence/notification. The executable fixture executes the copied monster initialization prefix in all difficulties, with simultaneous run levels and a simulated later HUD detour. It checks both localized portal-label paths with a destination above 255.

`run-cube-map-ui.cmd <output directory> <Exocet font path>` runs the completion scene with real ImGui draw data at 720p and 4K, checking viewport bounds, fades, dice, both outcomes and overlay expiry. It exports font atlas and draw geometry snapshots for visual inspection.

`generate-cube-map-monsters.py <D2R.exe>` regenerates the signature, relocation and chained unwind metadata for the supported executable. Per-run initializer copies preserve the native GS handler/cookie metadata and are released with their run.

The map executable fixture also runs the real native expiration routine on player and monster modifiers. It reproduces the old zero-duration removal, checks untimed effects through server/client expiration passes, repairs a removed monster list without healing combat damage, and combines the real native base/total stat APIs with sunder and a map resistance bonus.

### Timed shared map events

The map suite also covers authenticated participant readiness during simultaneous
act transitions, the full briefing countdown, clear/timeout behavior, the exact
5% secret selection and Ctrl+6 override, and Trial-only idempotent awards. It
checks five distinct picks from the 17 named boss types and six goblin classes.
Actual native quality routines execute with unchanged ratio inputs and sixfold
unique/set RNG thresholds. Native gold/superunique creation fixtures verify the
real argument order, monster-derived item level and preserved spawn bits. Midas
checks include guaranteed gold, sixfold value, null outputs and bounded arrays.
Fountain checks include arbitrary spell costs, charged items and mana drain.

The ImGui suite covers the three-second loading delay, readiness retries, the
embedded-art briefing, multiplying dice and pulsing red final-ten-second timer
at 720p and 4K. It exports command texture IDs with its draw-data snapshots.
`generate-cube-map-quality.py <D2R.exe>` regenerates the guarded native quality,
creation and mana signatures plus relocation/chained-unwind metadata.

Goblin event regression fixtures use the exact live RMD flags `0x245a0cb`,
including `neverCount`. They verify all six goblin classes remain eligible,
explicit objectives contribute to census and level finalization until death,
and ambient/ordinary-map monsters keep their previous exclusion. Cancellation
reason packets reject invalid enum values and their UI text fits 720p/4K.

`run-crash-diagnostics.cmd <output-directory>` runs an isolated child process
that faults deliberately. It verifies the original exception address/module,
register and stack capture, event checkpoint, and the minidump's exception
context, rather than the exception handler's own stack. Test captures stay in
the supplied output directory. The ImGui suite exercises all seven event
briefings through countdown zero at 720p and 4K.

The startup suite also reproduces transient module snapshots returning
`ERROR_BAD_LENGTH` before success, checks the retry deadline, and verifies
that permanent errors such as access denied return immediately.

Regular-event fixtures check all five ordinary events use the native level
population path and suppress fixed monster/superunique presets. Their randomized
rosters exclude bosses, allies, disabled and never-count monsters, even when an
ordinary base class also has a superunique definition. Trial and Goblin Grotto
clear the ordinary roster and retain their dedicated encounter pools.

`run-extended-classes.cmd <output-directory> <D2R.exe>` executes the audited
native class parser, inventory mapping, frontend empty-preview instructions,
player-allocation guard, creation registry and skill-cache allocation. It checks
Warlock ID 7, preservation of the original classes, rejection of class 8,
signature mismatches, and restoration of the patched operands. See
`docs/warlock-class.md` for the isolated asset staging and its current scope.
The optional third argument supplies a captured class-7 `.d2s` file for read-only
header validation and skill/item-section alignment checks. Native writer/reader
fixtures reproduce the earlier zero-skill body's incorrect 30-byte header,
verify the corrected count and narrowly scoped compatibility path, and preserve
original-class, nonempty-skill and truncated-input behavior.

The cube-map suite also covers temporary event automaps: bounded/ordered
multipart snapshots, deaths and stale/foreign packets, all event exit paths,
authenticated multiplayer delivery and re-entry recovery, untouched source
tile/room exploration, and drawing through private trees. It executes the
actual native tile-coordinate extraction, map-tree traversal, unit projection
and `units.sprite` draw call to verify native pan/zoom/clipping and gray-blue,
green and red frames. The current-layer pointer is never changed; ordinary
levels and map runs use their original trees.

The event UI fixture also leaves a 12-pixel default font selected while drawing
the live timer at 720p/4K. It checks the actual emitted vertices reference the
32-/64-pixel glyph UVs and retain their native raster width, catching accidental
bitmap upscaling independently of font-size selection.
Every event briefing retains die geometry through countdown zero; a separate
draw-data check verifies the die keeps moving after its roll has settled and
that it disappears during event entry and active play.

Sanctuary Survivors tests in `cube-map-survivors-test.inl` cover seven-class
roster persistence, profile bounds, XP/offers/replay and identity checks, Charsi
entry, empty-arena setup, mobile skill effects and movement preservation, kill credit,
boss/timeout transitions, and concurrent-run refusal. Avatar wrapper fixtures
check original gear/class/flags/hotkeys, autosave, character previews, shared
stash serialization, death, disconnect and failed creation. Native fixtures run
real Fire Ball actions/projectile generation and triggered-effect packets,
matching server aim and client targets while preserving movement, and player
assignment, including both
native save-buffer consumption calls. Existing real native stat-list tests
cover removal of arena movement/resistance bonuses. UI fixtures export class
selection, briefing and upgrade scenes at 720p/4K. `generate-survivors-native.py`
regenerates the audited avatar/save/skill entry windows. Full live class switching,
combat, return and Save & Exit/reload remain necessary beyond these fixtures.
Starter equipment tests require normal quality, omit mod starter charms and
staffmods, preserve normal-character creation and roll back failed placement.
Progression tests cover frozen server frames while an upgrade is pending, choice
handling without a deadline, five-point level budgets, base attribute capture,
class-specific life/mana/stamina restoration, Energy skill levels and migration
of version-one rosters without resetting levels, XP or ranks.
Client pause checks cover animation/world suppression, preserving fractional
world time without replaying paused intervals, normal resume, and town/foreign/stale/
other-event exclusions.

Fire Ball tests also resolve RMD-MP's moved skill ID for learning, selection,
casting and VFX, with no fallback to Meteor when the named skill is missing.

Both classic and HD native paused-client branches run in the fixture, with both
actual pause flags explicitly cleared first (the executable's classic default
otherwise masks an incorrect flag address). The test preserves neighboring flag
bytes, per-player render bookkeeping and zero-delta HD interpolation while world
steps stay frozen. Actual native resume/town updates exclude a long paused
interval, preserve the 25-Hz fractional phase, and reset for a new native clock.
Pure clock checks cover repeated upgrades. These checks do not establish visual
camera smoothness; that still requires live verification. Scoped native pause
flags are restored. Server checks keep the wave paused after choosing
until an authenticated matching-offer Resume, and reject stale/replayed Resume.
UI exports include the separate stat allocation/resume phase at 720p and 4K.

Paused stat-allocation fixtures execute the actual HD character '+' callback,
native stat packet writer, five-byte allocation request handler and native
spending functions. They first reproduce the server-five/client-zero failure,
then publish the host's values and spend all five points across STR/DEX/VIT/ENG.
The world frame stays fixed, allocation remains paused, class life/mana gains
and profile attributes persist, and a sixth click cannot spend more points.
Stat publication stops once the allocation phase ends.

Survivors protocol v5 adds host-provided attribute totals and bounded allocation
requests. Host fixtures process the actual custom allocation command through
the native spending functions while world frames remain frozen, checking
foreign sessions/offers, replayed spent totals, excessive counts, all four
attributes, derived life/mana gains, bulk spending, Resume and post-Resume
exclusions. UI fixtures click visible '+' glyphs in the active HUD stat panel
at 720p/4K: pre-upgrade and empty-budget clicks are disabled; normal/Ctrl/Shift
clicks emit one/five/all point requests. Draw-data checks reject a full-screen
dim layer over the interactive controls.

The native pause fixture also runs the normal client's reconciliation branch
at a 17-ms render cadence against independent 40-ms server pump boundaries.
Two pauses with durations that are not whole ticks must preserve their common
phase, including subsequent town updates. Before whole-tick time exclusion,
the single-pause version reproduced 150 phase mismatches in all 150 post-resume
copies; the corrected native run has none. This verifies update timing, while
the user still needs to confirm the visible camera result.

Class-XP isolation regressions use distinct values for all seven profiles,
exercise the actual snapshot save path, reload from disk after every selected
class update, and compare the other six XP/rank/attribute records unchanged.
Entry packet checks cover repeated Sorceress/Paladin selections. Native-avatar
wrapper checks replace an earlier class's banked XP and pending upgrade state
with the newly selected profile during re-entry on the same normal hero.
Combat census and removed-corpse credit leave all inactive class builds unchanged.

## Selected auras and Uber display

The colleague's combined patch is ported into the normal HUD source build.
From an x64 VS developer prompt, run:

```bat
tests\run-selected-auras.cmd <output-directory>
tests\run-sunder.cmd <dependency-root> <output-directory> <D2R.exe>
python tests\paladin-leftskill-test.py
```

The aura fixtures cover slot selection, opposing-slot restrictions, native
cleanup/periodic helper decisions, item-aura exclusions and runtime flags.
The sunder suite also checks both Uber display guards, protection rollback,
native memory patch restoration and the installed executable's signatures.
The optional table utility tests preserve all other table bytes and refuse
overwrites, including with Python optimization enabled. See
[the integration guide](../docs/colleague-hud-updates.md) for live acceptance.

## D3 style Whirlwind

From an x64 VS developer prompt, run:

```bat
tests\run-d3-whirlwind.cmd <output-directory>
python tests\generate-d3-whirlwind-native.py --check-compatible <D2R.exe>
tests\run-d3-whirlwind.cmd <output-directory> <D2R.exe>
```

Helper and core fixtures cover native start eligibility, authenticated control
messages, replay/timeout/release, stationary movement, steering rollback,
preserved damage timing, native resource costs, direct item pickup requests and
the scoped authoritative handler bypass. The optional executable argument also
checks actual hook installation/restoration in a private mapped image, including
coexistence with the existing mana hook and signature rejection. These checks do not replace
the [in-game acceptance checks](../docs/d3-whirlwind.md).

## Persistent summons

From an x64 VS developer prompt, run:

```bat
tests\run-summon-persistence.cmd <dependency-root> <output-directory>
```

The dependency root is the existing `x64-windows-static` directory containing
`include` and `lib` (MinHook). Storage tests cover bounded versioned JSON,
character/fingerprint mismatches, malformed input, exact component arrays,
atomic replacement and clearing a previous roster. Core fixtures use owned
units to verify early folder creation without inventing a roster, write-error
reporting, and retained previous data when replacement fails. They also use
fresh-cast S1 animations to verify restored companions start in neutral while
normal casts keep S1. The other core fixtures use owned
units and mocked native calls to exercise save/rejoin, dead/removed minions,
temporary AI and modifiers, stock/custom summon families, skeleton mage
appearance, three-member Hydra-family custom companions, native pet limits,
skill changes, deferred room placement and disabling. The supplied native save
buffer must remain byte-identical throughout the fixture.

`run-cube-map.cmd` and `run-cube-map-ui.cmd` also compile the persistence module
to exercise its shared lifecycle integration with the existing feature owner.
The mapped executable checks independent save/load installation: changing an
unrelated Survivors function keeps its mode disabled while summon persistence
can install; changing either required loader/writer rejects persistence and
leaves both entries untouched. Each failing signature reports its native RVA.
Regenerate the compatibility signatures only from the audited executable:

```bat
python tests\generate-summon-persistence-native.py <path-to-D2R.exe>
```

The generator rejects a different SHA-256. These tests do not replace the live
save/rejoin and remote-party checks in [the feature guide](../docs/summon-persistence.md).


Map event updates exercise the 5% Trial / six equal regular outcomes,
one-minute Goblin/Soul timers, exact named event treasure classes,
Champion Clash native elite-only density, 512-group bounds/rollback, scoped
Chaos seal suppression and an untimed instance. Timed reward tests cover
participant ownership, coexisting/refreshed bonuses, expiry, avatar swaps,
ordered authenticated client packets and teardown. Actual native loot tests
cover root/child NoDrop and Picks (including deterministic negative Picks),
exact TC selection after native upgrades, gold creation observation beyond
caller array limits and untouched global tables. Native quality RNG verifies
both Treasure Trove thresholds together and preserved unique-first priority.

Plentiful Pirating also removes named `Blank` TC entries from private root/child
distributions. Native regression coverage includes high Blank weights,
independent classic/expansion probabilities, deterministic negative rows,
all-Blank termination, and untouched ordinary drops/source records.


The Whirlwind suite's executable argument also runs
`d3-whirlwind-restart-test.inl`: ordinary local Assassin restarts execute the
actual client start guard and native success/path-failure branches through our
hook. It checks exactly-once delegation, temporary state-54 masking/restoration,
unrelated state bits, replaced stat-list ownership and D3/other-class/remote/
inactive-server/build-mismatch exclusions. No running game is opened.


Mixed Z01 rune deposits are covered by `cube-rune-bag-test.inl`, run with the
cube-visual suite. An optional third argument to `run-cube-visual.cmd` supplies
an exported BIN-table directory and checks the actual 36-rune mod catalog.
Tests execute the actual native output routine through useitem selection,
exact rune destruction and bag publication, with inventory/network boundaries
stubbed; native serialization checks all four saved balances. Other cases cover
custom/inherited multi-stat runes, stacks, totals above 32767, whole-deposit
capacity refusal, ambiguous/missing values, other-item scope and rollback.
`run-restart-game.cmd <output-directory> "<D2R.exe>"` checks offline restart
sequencing, identity/teardown guards, cancellation and the real native entry
wrapper in a private mapped image. See `docs/restart-game.md` for live checks.

The legacy-transmog suite also runs `stack-split-test.inl`: quantity selection,
88 -> 87+1 -> 68+20, no-space rollback, cancellation, personal storage pages,
transmog priority, repeated/stale/malformed commands, changed ownership or
quantities, and submission through the window-thread packet sender. It checks
all seven additional compatibility signatures and the real native save-header,
bitstream, allocation/load-wrapper and mode-setting ABI in a private image.
The item-body serializer/loader and allocator are test boundaries in that fixture.

`run-stack-split-ui.cmd <output-directory> <sprite-fixture-directory> [font.otf]`
checks the visual and minimalist gothic quantity panels at three resolutions
and screen corners, native artwork frame separators/truncation, custom item
paths, no item image requests in minimalist mode, no modal/dim layer or automatic
focus, and two-/four-digit text entry with Enter and button submission.
See the stack-splitting guide for the seven installation-owned sprite fixtures.
The transaction suite also checks copied full item codes, region-only
mouse capture, drag completion, keyboard capture only during editing, and outside
click dismissal/pass-through. Restart tests execute the real pause-open entry
and widget visibility setters to verify TCP/IP hiding and offline restoration.


## Character quest journal

From a VS x64 Developer Command Prompt:

```cmd
tests\run-quest-journal.cmd C:\temp\quest-checks <cubemain.txt> examples\quests.json
tests\run-quest-kills.cmd C:\temp\quest-checks <supported-D2R.exe>
tests\run-cube-visual.cmd C:\temp\quest-checks <supported-D2R.exe>
tests\run-quest-journal-ui.cmd C:\temp\quest-ui <cubemain.txt> examples\quests.json <panel.rgba> <native-font.otf> <migrated-override_rules.lua> <RMD-MP-data-directory>
```

The journal suite checks per-character isolation, import once without rewards,
checkpoint rollback and corrupt-file refusal, counter/evidence persistence,
confirmed crafting, item movement, kill deduplication, Countess plus Thul,
menu observations, external reading acknowledgment, reward refusal/retry and
all 45 definitions. Display-only text changes preserve definition identity.

The private-image native kill fixture covers player/pet ownership, failed
transitions, synchronous corpse deletion and dead/revived/friendly exclusions.
The cube suite also includes native matching and output transactions, event-mode
retirement of proof rows, proof-free three-ingredient milestone selection,
consumption/placement failure, and fixed reward properties with actual native
value shifts and item/player propagation. HP and mana use the mod's real
9-/8-bit saved fields plus value shift, not artificial 32-bit item fields.

The journal UI fixture compiles the migrated filter as Lua and draws every
quest using native font/quest-log panel, tab and button assets at 960, 1920 and
3840 widths, asserts actual artwork draw commands and the non-RMD hidden state, capturing
representative draw data for visual inspection. These are controlled/native and
render tests; they do not substitute for live gameplay acceptance.

The first-save regression clears the new-character header timestamp between
load and save, then verifies the same journal path and completed progress reload.
Migration accepts an unused old timestamp journal, preserves matching claimed
progress and refuses unmatched claimed progress. A native ordinary useitem
fixture records the real bag/skull input codes only after successful placement;
preflight and placement failures grant no crafting evidence.

`run-legacy-transmog.cmd` also checks automatic stack socketing. It exercises
quantity conservation, no-space refusal, native failure/rollback, cursor stat
refresh, position/ownership/replay guards and remote TCP/IP players. The owned
executable fixture runs the actual socket entry/classifier/parser/insertion and
saved-item wrappers with explicit grid, property attachment, item-body codec,
allocation and transport boundaries. Native cursor-stat packets are decoded to
verify quantities 1, 88, 9999 and 100000 without byte truncation.

Act VI checks:

- `act6-native-storage-test.cpp EXE` runs original quest/waypoint storage,
  native quest descriptions and sixth quest-tab selection in an owned PE image.
- `extended-acts-test.cpp EXE` includes the production module and checks
  difficulty isolation, deferred native Baal awards, quest/waypoint updates,
  guarded credits returns, template selection and hook transaction ownership.
  Passing the source data directory as a second argument also reproduces the
  real five/six-row ActInfo startup snapshots and one-restart option changes,
  asserting that live layouts are never swapped to the next process's mode.
- `stage-act6-test.py SOURCE_DATA` validates six-act variants, 20 native layouts,
  and exact disabled originals without editing the source mod.
- `act6-town-test.py [EXTRACTED_DATA]` checks the authored DS1, a connected
  town footprint, entry/waypoint/stash/NPC placement, HD dependencies and closed
  boundaries. The optional archive checks actual DT1 collision and asset paths.
- The native fixtures cover retained Warp metadata and execute the hooked
  native tile entry with unlinked Act VI exits, including all eight exit slots.
  Other acts, connected exits and the town-entry marker retain native behavior.
- The native cube-map fixture also dispatches a real cross-act warp to town 240.
- All 161 shared native five-town classifiers run before and after the patch
  across level IDs -1..512 (165,508 cases), including cursor/NPC eligibility
  and server town restrictions. Only the additional town changes classification.
- Town content checks verify a freestanding waypoint's real COF selection
  bounds and the two new boundary mappings in the native automap table.

These fixtures do not replace live Act VI rendering, credits, or save/reload
acceptance. See `docs/act-vi.md`.
