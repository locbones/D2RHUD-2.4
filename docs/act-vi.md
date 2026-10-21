# Optional Act VI prototype

This unfinished Workshop prototype explores engine limits and supports future
modding plans.

Only installed UI theme variants participate in template switching. Historical
`D2RLAN/UI Theme` entries do not block startup when that optional theme folder
is absent. Required global layouts and existing theme edits are still checked;
missing-file errors identify the exact path.

**RMD-MP only.** Enable **Act VI** in **D2RHUD Options > Workshop**, then fully restart D2R
with a matching compiled six-act package, or use `-txt` when authoring its tables.
The option defaults off. With source tables, changing the checkbox prepares the next
launch's tables and strings on disk, while the current process retains its
loaded tables and matching quest/waypoint layouts. The startup injector reaches
its States gate after ActInfo has already compiled, so it must accept the
current five- or six-act snapshot. Native UI hooks and layouts follow that
snapshot. A full restart then applies the prepared preference.

BIN-only packages do not require the TXT templates or `-txt`. Their native
loaded act count controls the current mode; a six-act package also validates
its compiled ActInfo order and Act VI level/waypoint IDs. Changing between
five- and six-act modes requires installing the matching compiled package.
The checkbox reports this requirement and never reinterprets five-act BINs as
six acts or blocks an ordinary disabled five-act HUD startup.

If a preference was edited externally instead of through the checkbox, startup
prepares its data and retains the current loaded mode, with a restart notice.
It does not rewrite already compiled native tables or reject an ordinary
five-act launch merely because ActInfo is loaded.

With the option enabled:

- Baal's completed quest unlocks Act VI in that difficulty and its town waypoint.
- The native expansion-ending notification and credits close queue an offline
  return using the same character and difficulty. Ordinary menu Credits do not
  trigger this. TCP/IP games never use the automatic offline reentry helper.
- The sixth native quest/waypoint tabs expose Dawn's Refuge (240) and eight
  cloned areas. The town has its own authored 48-by-40 DS1 and matching HD
  scene, a central waypoint court, stash, four service NPC placements, and
  separate forge/market/archive/watch shelters. Existing stock art is reused
  in this new layout; Harrogath and the Pandemonium Fortress are unchanged.
  The freestanding waypoint uses object 238 and the same DS1 placement in
  both graphics modes. Native room automap generation paints the authored
  walls with explicit tileset mappings rather than the Fortress's stock image.
- Six placeholder exploration quests use native quest records 42–47. The sixth
  currently completes by visiting the cloned Worldstone Ruins, level 246.
  These are development objectives, without final quest scripting or rewards.
- Completing native quest 47, with Baal completed, awards the final native
  difficulty step. Baal's four server award stores and local title mirror defer
  that step while Act VI is active. Existing higher difficulty unlocks remain.
- Prototype exploration unlocks its nine waypoints on entering Act VI. The
  cloned areas currently use waypoint travel. Original Warp definitions remain
  present because native stair generation requires them. A scoped tile hook
  leaves only unlinked Act VI stair/door markers inactive; connected exits,
  town entry markers, and every other act retain native processing. Connected
  levels and final content remain to be authored.

## Saves and disabling

The native quest buffer already contains 48 records in 96 bytes per difficulty;
native waypoint storage already has 112 bits. This prototype uses six spare
quest records and waypoint bits 39–47. It does not add a save section, enlarge
these sections, or change the existing 32 KB save limit. Progress remains native
and per character/per difficulty; there is no Act VI companion save.

The original tables/layouts are restored on the next startup, and normal Baal
difficulty progression resumes. The modern native character loader clamps a
saved act outside the loaded ActInfo count to Act I; return to an original act
before disabling if you want to keep that starting town. Earned quest flags and
unlocks remain in the native buffers. Full save/reload acceptance is pending.

## Data pack

`tools/stage-act6.py SOURCE_DATA NEW_OUTPUT` produces an isolated pack from the
current unpacked RMD-MP data. It retains exact original bytes and creates six-act
variants under `data/Pandemonium/ActVI/{disabled,enabled}`. The manifest is
`data/Pandemonium/act6.json`. Distribute those templates and the manifest with
the matching DLL. Generated active-path files initially contain original data.

Startup validates every target/template hash before selecting data. Unexpected
manual changes are refused rather than overwritten; regenerate the pack after
editing its source tables/layouts. Switching templates requires `-txt` so the
compiled data agrees with the native hooks. The EXE and launcher are unchanged.

`tools/create-act6-town.py EXTRACTED_DATA NEW_OUTPUT` rebuilds the town assets
from the shared authored collision/placement layout. Its output is checked in
under `content/act6`. `tools/install-act6-pack.py PACK TARGET_DATA` upgrades a
known pack, preserves enabled/disabled mode, backs up replacements, and refuses
unknown edits. Close D2R before installing. The new DS1/HD scene must accompany
the matching DLL and templates when distributing this update.

## Verification and remaining acceptance

The supported executable is SHA-256
`E4E13570E60C0A65CB0469D6659BE8A6008F9433AC50DB1C4F94501D7D9016F8`.
Signature checks cover each native edit/hook and reject mismatches.

The waypoint crash investigation reproduced two native exit-tile assertions
and a sixth-act cleanup fault. Warp metadata is retained; unlinked Act VI
exits no longer enter native room-link creation. The native combined-DS1 cache
and its path table are relocated to six entries, including the reverse
destructor and absolute/RIP-relative readers. The adjacent original global is
preserved. Existing cache ownership returns to its original location if hooks
are released after the native tables have unloaded.

Native town recognition is also inlined throughout the engine. The patch
extends 161 verified complete five-town predicates, including cursor/NPC
eligibility, server interactions and town movement restrictions. It does not
change individual town-specific quest scripts or replace unrelated constants.
Owned executable fixtures run all those predicates over level IDs -1..512
before and after patching: 165,508 cases preserve all original classifications
and add only the configured sixth town.

Real native server DRLG checks in the isolated frontend passed three seeds in
each of three difficulties: 81 level generations, 3,438 room activations, town
entry at (1024,1024), waypoint object 238, stash object 267, four resolved NPC
presets, and clean Act destruction. No character or save was loaded. The
manual test DLL source is `tests/act6-world-probe.cpp`; its exact private-path
guard prevents using it in the normal game installation. This is separate from
rendered HD, player waypoint-click, NPC interaction and save/reload acceptance.
The updated isolated run also resolves the two new wall mappings through the
real native automap lookup (frames 61 and 60); the town asset test checks the
waypoint's stock neutral COF has valid local selection bounds.

The startup regression also covers the actual injector's ordering: five-act
tables already loaded with the option off, checkbox preparation without changing
live layouts, six-act startup, and a switch back to five acts. Real process
checks in an isolated mod copy passed the native startup handshake in both
modes, including six loaded acts/249 levels, and a subsequent disabled restart.

Owned executable fixtures exercise original quest loading, native quest packets,
112-bit waypoint access, sixth quest-tab selection, original quest descriptions,
native Baal credit with deferred progression, cross-act warp dispatch to level
240, and hook install/rollback. Backend fixtures check three independent
difficulties, quest completion, town waypoints, credits identity/timeouts and
manual-Credits exclusion. Data tests verify 20 six-tab layouts and byte-exact
disabled variants. Mapping, states, restart, transmog, and socket regressions
also pass.

The user confirmed Dawn's Refuge's automap, NPC interactions and waypoint
work in both HD and legacy graphics with the Oct 20, 17:03:16 build. Controller
interaction, full character save/reload and the complete Baal movie/credits/
reentry sequence still require gameplay acceptance.
The feature remains a prototype until those gameplay checks pass. Test it with
a separate test character before using an established Act VI character.
