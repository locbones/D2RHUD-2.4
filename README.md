# d2rhud

Similar to [D2RHUD](https://github.com/locbones/D2RHud) but adapted for use with the 2.4 version of client and TCP/IP support.

Enable **Quantity Right-Clicks** in **D2RHUD Options** (on by default).
Right-click a non-transmog stack to separate one item, then choose the final
quantity in the new stack. For example, choose 20 from 88 to keep 68 + 20.
Inventory, cube and personal stash slots are supported for the local game host;
full storage leaves the stack intact. See [stack splitting](docs/stack-splitting.md).

- Stat Display code can be edited in plugin/sample/sample.cpp
- Font Loading can be edited in D3D12Hook.cpp
- Used in conjunction with D2RLAN for customizations

Native HD item mappings now preserve all four item-code bytes on D2R 1.4.71510.
In `data/hd/items/items.json`, entries such as `abc1` and `abc2` remain distinct
from each other and from `abc`. Existing one-, two- and three-character keys
keep their original space padding. Use the full code from the item TXT row:

```json
{ "abc1": { "asset": "sword/long_sword" } }
```

The example is one entry in the JSON array. `sets.json` and `uniques.json` keep
their standard named set/unique keys and `normal`/`uber`/`ultra` asset fields;
the native lookup uses the full four-byte TXT codes to select the appropriate
upgrade tier. Inventory, ground/world and equipped visuals use the native paths.
No transmog stat/table is required for four-character codes, and HUD does not
regenerate `transmog_table.txt` when native support is active. Existing explicit
transmog rows remain available for cosmetic overrides.

The capacity patch is automatic for all mods and independent of Transmog
Visuals. HUD settings report native support, or expose the old Extended Itemcodes
fallback if the executable does not match. HUD must load at startup, before the
native HD JSON table is populated; late injection reports that a restart with
HUD loaded at startup is required. The on-disk executable and JSON/TXT files are
unchanged. In-game confirmation of all visual contexts and upgrade tiers remains
required for a mod's custom assets.

Buff Icons is available in the D2RHUD Control Center. Enable it, select from the
current character's state-bearing skills, then use the position/scale controls
or drag the preview to place the icon area. Choices are saved per character in
the mod's `HUDConfig_*.json`; closing the window saves changes.
The panel uses the HUD's heading/body fonts and 2x scaling at 4K. On-screen icons
and countdowns also follow that resolution scale; Icon scale multiplies it.
Optional skill-name labels can be placed at the top or bottom inside each icon,
with independent text size and color. Timer appearance controls provide above,
below, or inside-icon positioning, text size/color, seconds or clock formats,
and a configurable expiration warning. The draggable border fits the visible
icons and external timer text without extra window padding or empty timer rows.

The display uses the active mod's `skills.txt`, `skilldesc.txt`, `states.txt` and
`_profilehd.json` icon paths, with local game CASC assets as fallback. Original
RGBA/BC3 `.sprite` atlases are loaded directly. CascLib is a vcpkg dependency.
Timers read D2R 1.4.71510 state durations at 25 simulation frames per second,
including refreshes and pauses; untimed effects have no countdown.

Persistent Summons is available in **D2RHUD Options** and defaults to off.
When enabled, surviving permanent companions return in town after the next game
finishes loading. This includes custom classes and summon skills using the
native companion implementations. Dead/unsummoned minions, timed summons,
Ravens, Hydras and finite traps are excluded; permanent custom companions that
reuse those skill implementations remain eligible when their actual AI is
permanent. Iron Golem keeps its existing native save/load behavior.

Summon rosters use separate per-character JSON companion files under
`%LOCALAPPDATA%\D2RHUD\Summons`, isolated by installation and mod. The `.d2s`
layout and bytes are unchanged. Each roster must match the character save's
identity, checksum and size. Disabling the option and saving clears the roster.
Restoration respects current skills and summon limits, recreates native skill
stats/equipment, and preserves skeleton appearance/element; it does not carry
temporary buffs or exact current health between games. Offline games and games
hosted by this HUD process are supported. TCP/IP rosters belong to the host;
joiners cannot enable the feature for a host without it. See
[summon persistence](docs/summon-persistence.md) for storage and verification.

The combined colleague updates add selected left-slot auras, prevent assigning
auras to both selected-skill slots, and enable native Uber immunity/modifier
display. Item-granted auras remain independent of selected slots. The existing
linear sunder and dynamic immunity-label fixes are retained. See
[colleague HUD updates](docs/colleague-hud-updates.md) for integration details
and the optional Paladin table utility.

D3 Style Whirlwind is available in **D2RHUD Options** and defaults to off. Hold
Whirlwind's assigned mouse button to spin and steer with the cursor; click nearby
ground items with the other button to pick them up while spinning. The local
host character uses native damage timing and one native cast cost per second.
Item range and inventory checks still apply. See
[D3 style Whirlwind](docs/d3-whirlwind.md) for supported play modes and checks.

Show Stat Ranges is available in the control center, with a matching entry in
Hotkey Controls. Choose Toggle for a saved always-on/off setting, or Hotkey with
press-to-toggle or hold-to-show behavior. The hotkey starts unassigned. Range
color, text size, right-hand spacing, custom format, and whether to include fixed rolls are
saved in `StatRanges` in the mod's `HUDConfig_*.json`. Text and spacing follow the
same resolution scaling as the HUD.
Saved always-on mode and an activated hotkey toggle remain visible while HUD
menus are open. Hotkey mode also previews real item ranges while its settings
panel is open, without changing the hotkey state. Menu/text-field input does not
activate bindings.

Range format defaults to `[%d-%d]`. Its first `%d` is the minimum and its second
is the maximum, so `Min: %d, Max: %d` displays `Min: 20, Max: 30`. Use `%%` for a
literal percent sign. Formats are single-line, up to 128 UTF-8 bytes, with exactly
two `%d` placeholders; invalid formats fall back to the default. Combined damage
lines apply the format to each roll range, separated by ` / `. The format saves
as `StatRanges.Format`, updates the live preview, and participates in native
tooltip background sizing, including non-ASCII labels.

Inventory, equipment, stash and vendor tooltips retain the identity of the item
used by the native item-text builder; they do not rely on world-cursor selection.
The identity is checked again when drawing, and ambiguous identical text from
items with different property sources is left unannotated.

Ranges use the active mod's properties, unique/set, affix, skill, and localization
tables. Contributions are summed by stat and parameter, including multiple
prefixes/suffixes, automagic, socketed jewels/gems, and matching runewords.
Conditional set bonuses remain separate from permanent item properties.
Annotations use the native HD tooltip glyph/row layout and are drawn separately,
so they consume no tooltip characters. The native background expands to fit the
measured ranges, including their configured size and spacing, and moves with the
text to fit screen edges. Original stat alignment and wrapping are preserved.
Unknown/ambiguous descriptions and packed
properties such as charges or chance-to-cast encodings are left unannotated.
Crafted/tempered items are also left unannotated because their original crafting
recipe is not recorded; affix-only bounds would omit recipe contributions.

For the verified 1.4.71510 executable, stat-description generation is extended
from 1,024 to 16,384 bytes using owned buffers. Existing native callers still
receive a bounded, null-terminated 1,024-byte copy; the HD tooltip renderer restores
the full description and measures its height before drawing. All native output
limit instructions are checked before patching. Name/link buffers and individual
native stat-line formatter limits are unchanged. Legacy graphics retain the
original tooltip presentation.

The sunder mechanic includes the source port of the submitted
`D2RHUD-dynamic-immunity-labels.zip` fix. Immunity pierce subtracts linearly from
monster resistance at or above 100, stopping at the configured `SunderValue`
(default 99): 180 minus 60 is 120; 180 minus 85 is 99. Applied reductions are
tracked so repeated damage/hover/UMod updates do not stack, and removing or
changing equipment restores/recalculates the monster's resistance. Bookkeeping
is cleared on game changes and monster respawns.

Native immunity labels use the matching server monster's current resistance when
available, retaining the game's localization, colors, layout, and >=100 threshold.
The source build uses a guarded Detours hook restricted to the audited native
label reader call site, rather than the package's old DLL-relative binary patch.
Client monster stats are not overwritten. TCP/IP joiners without a local server
monster still depend on the resistance updates received by their client.

Automap layers above 99 are supported automatically for every mod on the verified
D2R 1.4.71510 executable. Mod authors can assign distinct `Layer` values in
`levels.txt`, for example 100, 101, or 1000. IDs retain their full positive signed
32-bit value (up to 2,147,483,647); no HUD setting or per-mod mapping is required.
Levels sharing a layer still intentionally share exploration.

Layers 0-99 keep the native save/load path. Higher layers use the existing
32-bit IDs in native automap records, bypassing the unsafe 100-entry lookup table.
Their exploration is appended to the same character `.ma0`-`.ma3` files, with
separate records for each layer and all four cell groups. The game's existing
`.map` seed history, difficulty handling, cache locks and disk flushes are retained;
no sidecar file or save conversion is needed. HUD must be loaded when playing
levels with extended layer IDs. This extends automap persistence, not other level
or level-type limits. Code signatures are checked before installing both hooks;
other executable builds require a separate audit. Automated compatibility and
round-trip tests pass; an in-game extended-level save/rejoin check is still needed.

Legacy item transmogrification is restored automatically on the audited executable
for games hosted by this HUD process (offline and local TCP/IP host). Set
`Transmogrify=1` and `TMogType` to the target item code in `armor.txt`, `misc.txt`
or `weapons.txt`. Right-click the source in the character's inventory to replace
it with a newly generated normal item at the source's item level. This is an
actual item conversion, separate from cosmetic Transmog Visuals; `useable` and
`pspell` do not have to be changed. Four-character target codes are supported.
The handler reads the game's compiled tables, including changes after a table
reload, rather than maintaining a separate TXT cache.
The tooltip's control footer displays `Right-Click to change to <itemname>` in
matching explicit grey below the other controls, using the destination
base item's localized name and retaining the name's embedded colours. The same grey
before and after the destination keeps the instruction and other controls consistent. Both
native text blocks render their logical lines in reverse order.

`TMogMin` and `TMogMax` retain the quantity behavior of the
[legacy routine](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/ITEMS/ItemMode.cpp):
equal nonzero bounds produce a fixed quantity (e.g. 200/200 gives 200); differing
nonzero bounds roll from min through max minus one; either bound zero gives zero.
The target starts in the original slot, falls back to another free inventory slot,
then drops nearby if the inventory is full. Invalid targets/bounds, foreign items,
equipped/stash/cube items and stale commands cannot consume the source. Creation
and placement use the server's native routines, retaining the original item until
the replacement succeeds and restoring it if no destination is available.

Cube recipes can apply a cosmetic world model with `useitem,vis=cap` or
`useitem,visual=Harlequin Crest` in an output column. A bare `vis=cap` also implies
`useitem`. Targets are base item codes (up to four characters), or the exact
`index` name from `uniqueitems.txt` / `setitems.txt`. The original item is retained
with its rolls, quality, sockets, runeword, seeds and item level; only a saved
cosmetic selector is changed in the item's saved state-zero modifier list, which
the native item serializer includes in packets and saves. Visual outputs request a full native item notification
so the client receives the changed saved stat even when `useitem` reuses the item ID.
The HD world model uses the target's normal,
unique or set asset. Inventory sprites and inventory dimensions keep their
original selection. This is independent of the older Transmog Visuals checkbox.

For persistence and client synchronization, the mod needs a cosmetic stat named
`hud_visual` (preferred) or the existing `transmog` stat. Configure `Signed=1`,
`Send Bits=16`, `Save Bits=15`, `Save Add=16384`, `ValShift=0`, and no stat
operations, callbacks or descriptions. Use an available stat ID below 511;
no hardcoded stat ID or automatic changes to the mod's tables are used. Positive
`transmog` values continue to address the existing `transmog_table.txt`; negative
values -1 through -12288 are reserved for cube visuals. Each target category
supports its first 4096 table rows, with a stable selector independent of table
counts. Keep existing row indices stable when updating a mod, as with other
saved item definitions.

Visual outputs preserve the input and therefore cannot include quantity, quality,
affix, socket, repair or other output modifiers. Existing `lvl`, `plvl` and `ilvl`
columns are ignored so the original item level is retained. Leave `mod 1` through
`mod 5` columns empty. Ingredients and normal input matching work as usual. Invalid
targets or unsupported cosmetic stat settings disable the recipe; conflicting
output modifiers are rejected before consuming ingredients. Compile/reload the
TXT tables with HUD loaded so the new output syntax is recognized. When a launcher
has already compiled the tables, HUD also reads the visual options from the active
mod's `cubemain.txt` and applies them to a temporary copy of the selected recipe.
It checks row counts and input counts before accepting this mapping; the shared
compiled table is not modified. Loose source changes are picked up on the next
transmute. Other players
need HUD and the same mod definitions to render the saved appearance. The
hand-axe to Griswold's Edge visual has been confirmed in-game, with the saved
selector matching on the client and server and reaching the model lookup.
Save/rejoin verification remains to be checked in-game.
Remote servers need their own authoritative implementation; a HUD-only TCP/IP
joiner cannot enable this on an unmodified host. The General panel reports hook
availability. A DLL restart is needed to load the handler, but no save conversion
or separate per-mod HUD setting is required. Native-mock regression tests and the
Release build pass. Z02/Z03 conversion has been checked in a live local game,
including matching client/server item IDs and base classes after repeated clicks.
Stack quantities, full-inventory ground fallback and save/rejoin still need live
coverage beyond the automated tests.

States tables support up to **512 data rows (state IDs 0-511)** on the audited
D2R.exe version 1.2.69270 (SHA-256
`2C2DDB60A40EDE3AAFF51AFB64E932621A03E32487687B6C64D88205FA652360`).
This extension is automatic; General reports whether its hooks are available.
Executable signatures are checked before changing the loader limit, and an
unsupported build retains its original limit.

Use the updated `Pandemonium.HudLoader.exe` alongside this HUD build. The older
helper's 1500 ms injection delay can reach the original assertion before HUD
loads. The updated helper holds the native check at `0x1DC277`, loads HUD after
mod globals are available, verifies all state hooks and the 512-row limit, and
only then resumes table loading. It leaves the executable and tables unchanged
on disk. See [startup helper](d2rhud-injector/STARTUP.md) for building/launching it.
Late attachment through the old attach-only injector cannot fix this startup
assertion. Exit D2R fully
before changing the number of state rows; do not add/remove rows with Reload
Tables while a character is loaded. Preserve existing state IDs when expanding
the table. Existing native unit storage accommodates both 512-bit state masks;
the extension replaces three smaller temporary snapshots and adds ninth-bit
state synchronization while retaining native client stat decoding and callbacks.
Tables with 255 or fewer rows keep their original synchronization path.
An extended-state multiplayer game requires this HUD and matching state tables
on its host and every client. This does not raise the item stat ID limit.

Tests execute the native activation/removal and packet decoding paths in a private
executable image, including state 511, all 512 active flags, curse removal and
hook rollback. Live startup checks now load RMD-MP's expanded 259-row table and
an isolated 512-row test mod without the original assertion. High-state gameplay
and multiplayer remain to be verified. The test mods were archived outside the
game's Mods folder afterward; RMD-MP's table was not modified by these tests.

## Shared cube maps

The cube output `map=<level ID>` creates a shared map run with cross-act red
portals, refreshed monsters, temporary player/monster modifiers, a remaining
monster counter and automatic town returns on completion. One run is allowed
per game; occupied destinations are refused before recipe consumption.

See [cube map setup and behavior](docs/cube-maps.md), including the required
`hud_cube_map` state and matching multiplayer client tables. The native fixture
tests pass; complete in-game traversal and multiplayer runs still need checking.

## Sanctuary Survivors

Approach Charsi in Act 1 for a native survival-arena entry panel: automatic
class skills, a saved seven-class roster independent of the entry character,
growing waves and a ten-minute final-boss challenge. It uses the shared event system and required `hud_cube_map` state.
See [Survivors setup and controls](docs/survivors.md).

Special Thanks to:
- scizzydo for resize logic
- Killshot for chat and detour structures
- [dschu012](https://github.com/dschu012) for D2R base configuration.

![D2RHUD Example](D2RHud_Example.png)


A Z01 Storage Bag plus any mixture of runes now deposits them in one transmute.
The helper totals the loaded single-rune values across all four tiers and uses
the original bag through native useitem. See [mixed rune deposits](docs/rune-bag-deposits.md).
Restart Game adds a pause-menu shortcut for saving and entering a fresh offline
session with the same character and difficulty. See [installation and checks](docs/restart-game.md).


### Character quest journal

In **RMD-MP only**, the Pandemonium side menu tracks the 45 tutorial quests per
character with native quest-log panel/button artwork, using
server kill/crafting events, owned items and menu visits. Ordinary proof and
turn-in recipes are retired; actual crafting lessons and the three charm
upgrades remain. Install the matching `examples/quests.json` as
`data/D2RHUD/quests.json`. See [quest-journal.md](docs/quest-journal.md) for
legacy import, companion saves, deployment and compatibility.

An optional RMD-MP Act VI prototype adds native sixth quest/waypoint tabs,
cloned exploration areas, Baal-to-Act-VI progression and a deferred difficulty
unlock. It defaults off and requires a full restart when toggled. See
[Act VI setup, prototype content and verification limits](docs/act-vi.md).
