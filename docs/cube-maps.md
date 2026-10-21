# Cube map recipes

Set the recipe's `output` column in `cubemain.txt` to `map=<level ID>`.
For example, `map=2` opens a map to the Blood Moor. `useitem,map=78`
opens a Flayer Jungle map and preserves the native `useitem` output.
Ordinary item outputs in `output b` and `output c` work alongside the map.
Use **`map=zzz`** (or `useitem,map=zzz`) for a random destination. Selection
uses the loaded level tables for the current difficulty and includes eligible
custom areas. It excludes all Torment Trials, Icy Hell, Uber Tristram and Forest
Ruins, plus towns, empty populations and levels without valid geometry.
Explicit numeric destinations retain their existing behavior. The reusable
recipe remains random on every transmute; its resolved destination is stored
only in that invocation's map run.
Item modifiers on the map-bearing output require `useitem`; a standalone
`map=` output has no item to modify. Choose the recipe's inputs
as usual. Generate the tables with HUD loaded, or retain the matching loose
`cubemain.txt`; HUD can recover its options from that source when the table
was compiled before injection.

For an identify-scroll map to the Flayer Jungle, use `numinputs=1`,
`input 1=isc`, and `output=useitem,map=78`. Existing `Ev12` outputs in the
other two slots, including their item levels, can remain. Only one map may
be created by one recipe. Visual-only outputs cannot accompany a map.
Invalid destinations or occupied areas refuse the recipe before ingredients
are consumed or ordinary outputs are created. An invalid `map=` expression disables the
recipe during table loading; fully restart D2R with `-txt` after correcting
it. The header and single example row in
[`identify-scroll-map-recipe.tsv`](../examples/cube-maps/identify-scroll-map-recipe.tsv)
are for copying into an existing table, not replacing the whole file.
The alternative
[`identify-scroll-random-map-recipe.tsv`](../examples/cube-maps/identify-scroll-random-map-recipe.tsv)
uses the same inputs with a random destination. Choose one example row rather
than adding both recipes with identical inputs. Restart with `-txt` after
changing `cubemain.txt`.

Transmute in a town. Each game has one shared map run at a time. Everyone inside uses
the same rolled modifiers and remaining-monster count. The destination may
belong to another act. Invalid destinations and occupied destinations are refused
before consuming the recipe inputs. An adjacent player observing a room,
a player corpse or a player's pet also prevents that destination's reset.
After the map and its following event have finished, the same destination
can be cubed again immediately; there is no once-per-level/game restriction.
An unfinished ordinary map can also be replaced from town once every player,
corpse, summon and room observer has left it. This abandons the old run,
removes its portals/modifiers and rolls a fresh layout and population.
If the new recipe uses another destination, the abandoned area's ordinary
monsters are restored. Active special events still have to finish.

On the first successful entry, HUD captures everyone currently in town.
Both red portals close after all those players have reached the map.
Loading players keep the portals open; disconnected players no longer hold
them open. The automatic return to town after a clear still works. Special
event and Survivors portals retain their separate entry/return behavior.

Every new map recipe rolls a distinct per-level layout seed and discards the
previous layout's room graph. Maze shapes and randomized outdoor/preset
pieces change where the native generator permits it; fixed maps and outdoor
act connections retain their native constraints. The act-wide seed remains
unchanged. Act III's Spider Forest, Great Marsh and Flayer Jungle also reroll
their precomputed river/clearing paths using native-compatible preset tiles.
The paths remain connected, with the original exterior exits, head/tail row,
area bounds and number of clearings. Unknown/custom template formats retain
native generation. The host skips repeated jungle templates when choosing
the next seed. The host sends the seed before entry and requires an authenticated
matching client acknowledgment. All players need this HUD build. A separate
game epoch clears old client plans even when offline games reuse a world seed;
ordinary act transitions retain the current game's plans.

Before retiring a room graph, HUD invalidates cross-level neighbor/warp caches,
unlinks the old rooms from native status lists, clears the room iterator and
redirects level links to the replacement. Live client-room references prevent
a reset until their normal teardown finishes. `map-events.log` records each
generated level's server/client seed, room count, sorted room-rectangle hash
and jungle-template hash. The regular outdoor room grid can retain the same
rectangle hash while its river paths change; compare the jungle hash for those
areas. Exterior act connections and fixed layouts retain their native constraints.

Existing ground items in a destination are moved intact to the recipe user's
town position before its layout is replaced. An unused destination is not
populated just to discard it; cleanup restores saved inactive units without
generating extra preset gold or objects. After a clear and return to town,
HUD waits for players, corpses, pets and adjacent room observers to leave,
then restores the area's normal native monster population in the rolled
layout. Map modifiers and scaling no longer apply. Existing ground drops
survive that population refresh. This respawns the normal population rather
than restoring the area's exact prior kill/health state.

Rolled-layout exploration is cached in memory by game/layout seed. It cannot
load the ordinary area's stale outlines or write temporary layouts into the
character's normal `.ma` files. Character save formats remain unchanged.

## Temporary state

Add one state named **`hud_cube_map`** to `states.txt`, using the next unused
ID. Set `aura` to `1`, leave `nosend`, `pgsv`, `curable`, `curse`, `remhit`,
`plrstaydeath`, `monstaydeath` and `bossstaydeath` empty or `0`, and set
`*eol` to `0`. Other fields can remain blank. This state has no skill or
overlay. It carries the temporary rolled stats using normal state packets.

Use the same tables and HUD build on the host and every joining client.
IDs 256–511 require HUD's extended-state startup support. A missing or
unsendable state makes the recipe refuse to run. Restart after changing
`states.txt` so both the client and server load the same state layout.

## Run behavior

HUD uses native room unload/reload and inactive-item restoration. Monsters
are regenerated before entering, including rooms outside the player's
current view. Existing dropped items and non-monster objects are retained.
Each run selects a shuffled roster of up to eight random monster types from
the mod's ordinary level spawn lists. Native placement, groups and minions
remain in use. Bosses retain their direct encounter behavior. Disabled
monsters, NPCs, allies, bosses and `nevercount` entries cannot enter the
random roster. Unrelated areas keep their original populations.

Each ordinary map contains **8–13 champion/unique packs**. Existing named
unique packs count toward this total; a champion group counts as one pack,
regardless of how many champions it contains. Scripted act bosses remain
separate objectives. HUD suppresses the destination's random elite rolls only
during map population, then places complete native champion groups or unique
leaders with minions away from the entrance. Normal area settings are restored
immediately. The HUD modifier list shows the initial pack count.
Maps that cannot fit the required packs, have more than thirteen existing
named packs, or contain custom scripted champions whose groups cannot be
identified refuse preparation before consuming recipe inputs. Special events,
Survivors and normal population restoration retain their own population rules.

Monster level starts at the map creator's character level **+3**, captured
when the recipe is used. A rolled additional-monster-level modifier adds
another **2–6** levels. The final level is selected before native monster stats
and skills initialize, and appears in the modifier display. Levels respect
the native monster packet's maximum of 255. All players share this level.
The rooms stay resident for the run, so unvisited rooms cannot disappear
from the objective count. Monsters that revive or are newly summoned are
included; pets, NPCs and monsters marked `nevercount` are excluded. A missing
monster is kept outstanding until a death is observed.

Each run rolls three monster effects and up to four player effects, using
the original ReMoDDeD affixes and additional themed conditions. Only stats present
in the mod, with sufficient signed network widths, can be selected. Effects
include life, defense, resistances, speed, skill levels, crushing blow,
player penalties, and magic-find, gold and experience rewards. These modify
real stats on untimed state lists that HUD explicitly removes. They do not
use a zero-duration timer, which would expire and repeatedly reapply them.
Monster health is filled only on the first application; repairing a removed
modifier list preserves combat damage. Sunder adjusts base resistance by
the required difference, preserving the map/aura contribution without adding
it twice. Player effects are removed on leaving
or death; returning to a map cannot heal the player through repeated
maximum-life changes.

Additional conditions include:

- **Treasure Surge:** 1–3 extra root treasure-class picks for each monster.
- **Glass Cannon:** +40–80% player physical damage and −10–20% maximum life.
- **Battle Frenzy:** +15–35% player attack and cast speed.
- **Blood Feast:** +4–10% life stolen per hit and +2–5% mana stolen per hit.
- **Second Wind:** +30–70% mana regeneration and +15–35% hit recovery.
- **Fleet Footed:** +15–35% player movement speed.
- **Featherweights:** −15–30% monster life and +15–30% movement speed.
- **Heavy Hitters:** +20–45% monster physical damage.

Conditions that modify the same stat on the same side cannot roll together,
so opposing speed/life affixes do not silently cancel a theme. Paired conditions
require both stats to exist with sufficient network widths.

Treasure Surge uses a private copy of the selected root TC after native
difficulty/monster-level upgrading. Nested classes, quality, NoDrop and RNG
remain native; extra picks are additional chances rather than guaranteed
items. Negative deterministic Picks retain their normal drops before bonus
weighted picks. The usual six-item cap expands to seven through nine for this
effect, with bounded output arrays. Global TC records, mod tables and save data
are unchanged. Ordinary maps alone receive this effect; it does not replace the
separate event or Survivors drop rules. All players need this HUD build; map
snapshot/command protocol version 5 rejects older event/modifier definitions.

The top-right terror-zone text becomes the map title and modifier list.
A scaled counter above the lower HUD displays the remaining monsters.
Map monsters do not receive HUD terror-zone scaling or terror treasure
classes. Map portal labels use the destination's localized location name,
including custom level IDs above 255. Ordinary portals and recipes retain
their native behavior.

Scripted bosses are spawned directly when their previous quest trigger has
already been used. Diablo does not require replaying the seals. A Throne of
Destruction map uses a direct Baal fight in place of replaying the waves.
Saved quest progress is not reset. Native Uber boss placement is retained.

After a clear remains stable for two game seconds, players in the map return
to their saved town positions. New spawns or revivals cancel that transition.
The return waits for act transitions to complete and for dead players to
revive. Any red portals still waiting for the party close after the returns
finish. An active map continues if a player leaves early; portals already
closed after party admission stay closed.

## Shared special events

Completing a map no longer grants stat points for an ordinary six. The server
rolls one shared die for the party and independently checks a **5%** secret-event
chance. Participants must be in town and report three continuous seconds of
client readiness before the screen fades directly into the dice sequence.
Loading does not consume this delay. The caption reads **"Rolling the dice of destiny..."**
above the ornate animated die and embedded event artwork, followed by an event briefing with a **10-second
countdown**. The chosen die keeps gently rocking in the central circle throughout
the briefing, with the event title above and description below it. It disappears
when event entry begins. The secret outcome animates multiplying dice.

Trials of Tribulation retains its **5%** chance. The remaining **95%** is
split equally between these six die outcomes (about 15.83% each):

1. **Goblin Grotto:** all treasure goblins, Cow Level (39), **one minute**.
2. **Treasure Trove:** **+500% set and unique roll chance**, anywhere,
   **ten minutes**. Both quality thresholds are six times normal; native
   unique-first priority and the rest of item generation are preserved.
3. **Golden Goose:** **+500% gold amount**, anywhere, **ten minutes**.
   Naturally generated gold piles have six times their normal quantity;
   this does not add forced gold piles.
4. **Champion Clash:** champion and unique packs throughout Chaos Sanctuary
   (108), **this instance only, with no timer**. Native density and complete
   elite groups/minions are retained; ordinary packs and quest boss triggers
   are suppressed. Clear the instance to finish.
5. **Plentiful Pirating:** **NoDrop=0 and +1 Picks in every visited treasure
   class**, including nested classes, anywhere, **ten minutes**. Negative
   deterministic Picks retain their original outputs and add a weighted pick.
   Native item caps and bounded output arrays remain in force. References to the mod's
   `Blank` treasure class are treated as NoDrop: their probability weight is
   removed at every depth, preserving the relative weights of real drops.
   Outside this bonus, `Blank` keeps its normal player-count-independent behavior.
6. **Soul Surprise:** teleport to **level 239**, **one minute**, populated with
   random eligible ordinary monster types and native groups/elite rolls.

Arena random monsters use the exact loaded treasure class **`TormentGroupA`**.
Champion Clash uses **`ChampClash`** instead. The supplied initial `ChampClash`
row is a one-pick reference to `TormentGroupA`; replace its contents to customize
Champion Clash drops. Add the row to the mod's own `TreasureClassEx.txt`, keeping
that file's columns. The fragment in
`examples/cube-maps/champ-clash-treasureclass.tsv` illustrates this row; it is
not a replacement for the complete table. Restart with **`-txt`** after editing.
Missing required rows cancel the event with a specific diagnostic.

The three location-free bonuses are granted to the eligible party in town
after the briefing. They follow each recipient between areas for ten minutes
of game time, including subsequent maps. They require no arena or teleport
and do not block opening another map. Different bonuses coexist; repeating
one refreshes its duration rather than multiplying its strength. The HUD shows
each remaining timer. Random hostile monsters killed by a rewarded player use
`TormentGroupA`; named quest encounters retain their own tables. Loot bonuses
also apply to containers without replacing their tables. Bonuses end when the
game closes and do not change character save files or global TC records.

Arena destinations are safely reset before admission with the same occupancy,
corpse, pet and observer protections as normal maps. An occupied destination
cancels the event instead of disrupting another player. Fixed monster presets
are suppressed. Goblin Grotto places up to 120 goblins; explicitly spawned
`neverCount` goblins still count as objectives. Event monsters initialize at
the map creator's captured level **+5**, except Trial's level 125. Objects and
existing ground drops survive the reset.

Arena participants enter automatically after the briefing. Linked portals
allow return and re-entry while the event is active. Timers begin after the
participating clients finish loading. Timed arenas end on **clear or timeout**;
Champion Clash ends on clear. The remaining-monster counter stays above the
lower HUD. Timed arenas show a countdown, red and pulsing in the final ten
seconds. Terror-zone scaling, drops and text stay excluded from arenas.
After the party returns, normal area population restoration uses the same
occupancy safeguards as ordinary maps.

Event HUD text uses separate font faces rasterized from 16 to 128 pixels in
the mod's UI font (with an embedded fallback). The active timer uses native
32-pixel glyphs normally and 64-pixel glyphs at 4K, with pixel-aligned placement,
rather than enlarging the default 12-pixel atlas. Briefing countdowns and the
remaining-monster counter use the same font-size selection.

### Temporary event automap

Inside a special event arena, opening the automap shows the complete
layout and all living hostile monsters, including those outside the client's
nearby rooms. Normal monsters use dark gray-blue markers; champions and minions use
green; uniques, superuniques, bosses and Trial objectives use red. These are
the existing frames 3/1/5 in `hd/global/ui/automap/units.sprite`.
The native automap handles their size, zoom, pan, clipping and minimap position;
the ordinary automap show/hide controls still apply.

The host sends a read-only tile snapshot once per participant and current
monster positions every five simulation frames (normally 200 ms). Snapshots
use bounded, versioned fixed-size packets and replace the previous positions
only after every part arrives. Dead monsters disappear on the next update.
Re-entry/client loading can request a fresh layout without changing the event
timer, state or rewards. Host and clients need this HUD build.

This reveal applies **only during event entry and active play, in its matching arena (39, 108 or 239)**.
It stops immediately in town, in another level, on event return/cancellation,
at the timer deadline, or when the client leaves the game. Ordinary map runs
and ordinary cow levels retain their usual exploration. The extra geometry
lives in private HUD trees passed only to the draw routine: no native layer
list, revealed tile flags, RNG state, or `.ma` exploration save is changed.
Native discovery during actual play continues normally and remains the only
exploration written to the game's map files.

### Trials of Tribulation

The secret event chooses **five different monster types** from the loaded
Icy Hell named boss presets, the five act bosses, Mad King and Moophista.
The Icy Hell pool is Enderaku, Majiko, Rakuranth, Grizzlepaw, Kelthir,
Forsaken Izual, Deddomer, Gurka, Queen Syndred and Anuburak. They
initialize at **level 125**, with at least **three random resistances raised
to immunity** per objective boss. Existing sunder behavior still applies.
Only the five objective bosses count toward this Trial; their summons do not
change the objective. Defeat all five within **two minutes** to win.

Only a successful Trial permanently grants **+2 unspent stat points** to each
connected eligible participant. The authoritative server ledger prevents repeat
grants, and rendering/client readiness packets cannot award points. Failure,
timeout, cancellation and regular events grant no stat points. Players return
to their saved town positions and the event portals close.

For video demos while hosting or in singleplayer, hold **Ctrl+6** (top row or
numpad) through map completion and the return to town. With D2R foreground,
this forces the shared **Trial of Tribulation**, replacing the previous forced
six/stat reward. Release it for normal random event selection. A remote
client's keyboard cannot change the host's outcome.

Use the same HUD build and mod tables on the host and all clients. The event
protocol uses fixed-size native packet framing with a version check. This
implementation is guarded for D2R **1.2.69270**; unsupported executables leave
map recipes disabled. Replace the DLL while the game is closed.

Event loading checkpoints are kept in `%LOCALAPPDATA%\D2RHUD\map-events.log`
(bounded to approximately 1 MiB). An unhandled exception saves `crash.log`
with the original fault address, module offsets, registers and stack addresses,
plus a normal `crash.dmp` with thread contexts. These files remain local;
there is no automatic upload. The latest crash replaces the prior capture.
