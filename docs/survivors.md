# Sanctuary Survivors

A native D2R survival arena, inspired by [D2 Survivors](https://d2survivors.lol/)
and Vampire Survivors. This unfinished Workshop prototype explores engine limits
and supports future modding plans. It defaults **off**. Enable **Survivor Arena**
under **D2RHUD Options > Workshop**, then approach **Charsi in Act 1**, choose one of the seven
original classes in the HUD panel, then enter. Warlock is excluded from this
roster. Close native NPC interaction windows and put away any cursor item first.

The preference is saved as `SurvivorArena` in the mod's HUD configuration;
missing preferences default to false. In TCP/IP, the host must enable arena
entry too. Disabling it hides the entry menu and rejects new entries, while
existing runs retain their controls and can finish or return to town. Ordinary
cube maps and character save hooks remain available.

## Separate class progression

Entry creates a separate native player with plain class equipment,
base stats and skills. A new class starts at **level 1**. The normal character
used to approach Charsi does not determine the class or its progression.
The selected class resumes its saved level, XP, upgrade ranks and allocated attributes on later runs.
Only the class currently participating in the arena gains XP; inactive class
profiles do not. Party XP can go to other active participants' selected classes.
XP earned before leaving remains in that class's profile, so unspent earned
level-ups can produce upgrade choices immediately when that class returns.
Normal equipment, stash contents, gold, quests and learned skills are not copied
to the arena avatar. Normal-quality class weapons and shields are generated
fresh each run. Mod charstats rare equipment and utility charms are excluded,
including starter affixes such as Attacker Takes Damage.

The seven profiles live in one local roster **per mod and Windows user**:
`%LOCALAPPDATA%/D2RHUD/Survivors/<mod>.roster` (`base.roster` without a mod).
Every normal character on that installation uses the same roster. In TCP/IP,
each client supplies its selected local class profile and persists its own
host-validated snapshots. This is local mod save data, not a competitive
server-authenticated account system. Both host and clients need this build.

XP is saved periodically (about every two seconds), on level changes, on return,
and when the client tears down the game. A crash can lose the most recent
unsynchronized XP. Files have a version, length checks, profile bounds and
checksum. Writes replace atomically and retain the prior valid file as `.bak`.
An unreadable, corrupt or unknown-version roster blocks entry rather than
silently resetting progression. Restore its backup to recover it.

The normal player unit and its inventory, skills and stat lists remain intact
outside the room update list. Saves and character previews route through that
original unit and its original class, flags and hotkeys. Return, arena death,
Save & Exit and disconnect restore the original player. The temporary avatar
and its native inventory are freed. Arena death does not mark the normal hero
as dead, including a Hardcore entry character.

## Combat and controls

A ten-second briefing precedes an empty cow arena. The usual three-second
arrival handshake runs before the timer and waves start. Move normally while
native class skills attack. Automatic attacks execute native damage and
projectile effects immediately while preserving the player's movement path.
Their triggered-effect packets show the spell effects on every nearby client,
including the owner, without starting a movement-stopping player cast animation.
Automatic skills do not
require mana; throwing stacks and equipped durability are maintained only on
the arena avatar.

The primary skill is learned and selected on the right skill button at entry,
including at level 1. Learned skill levels are synchronized explicitly after
the player switch and on upgrades. The automatic effect temporarily supplies
its skill and enemy target, prepares native damage, runs the action immediately,
then restores the prior skill, aim and target. Failed preparation sends no
visual effect and does not consume the automatic cooldown.

The default skill slots are Lightning Fury / Plague Javelin / Charged Strike,
Fire Ball / Ice Bolt / Charged Bolt, Teeth / Bone Spear / Poison Nova,
Blessed Hammer / Holy Bolt / Fist of the Heavens, Bash / Battle Cry / Berserk,
Fissure / Volcano / Tornado, and Fire Blast / Shock Web / Wake of Fire.
Sorceress's Fire Ball is resolved by its compiled skill-table name, including
RMD-MP's moved ID 41 instead of vanilla ID 47. Other slots use the original
class skill IDs; native class ownership is validated
before switching players. Loose mod `skills.txt` names appear in the menu,
including RMD-MP's renamed skills. Damage and projectile mechanics remain those
of the mod. Barbarian's second slot uses Battle Cry because Whirlwind controls
the movement path. Native movement callbacks are rejected before avatar entry.
Melee and radial skills acquire targets within a shorter effective range.

Kills grant separate Survivors XP, shared by ready, living participants in the
arena. Native monster experience is zero. Level up by clicking an offered
upgrade or pressing **1**, **2** or **3**. While an eligible participant has
an upgrade pending, the shared server simulation and run timer pause. A Survivors
character-stats panel appears on the left alongside upgrade choices on the
right. Both are active HUD windows, with no full-screen dim layer over controls.
Choosing a skill grants the new level and five stat points while keeping the
wave paused. Spend them with the stat panel's **+** buttons: a click spends one,
**Ctrl-click** spends up to five, and **Shift-click** spends the remaining budget.
The host verifies the participant, run, offer, current spent total and remaining
points, then calls the native attribute-spending function. Replayed or excessive
requests cannot spend points twice. Class life/mana/stamina gains still use native
rules. Actual totals and allocated bonuses are displayed from host snapshots.
Click **Resume wave** when ready; unspent points may be kept for later. Native
stat and vitals synchronization also continues during allocation. Choices
have no deadline or automatic selection. Network commands and exit handling
continue while paused. Class level and upgrade ranks persist,
up to level 30. The first three upgrades unlock or strengthen the class skills.
Empower adds two effective skill levels per rank; Quickening shortens automatic
recharge; Far Reach extends target acquisition. Fleet Footed, Mending and Iron
Ward improve movement, healing and physical resistance. Native avatar level
and life/mana/stamina grow with Survivors level; normal hero stats do not.
Each gained level grants **5 stat points** in the Survivors stat panel. Strength and Dexterity retain their native combat effects; Vitality
and Energy retain their class-specific life, stamina and mana gains. Each ten
allocated Energy additionally adds one effective level to unlocked class skills,
so Energy is useful with free automatic casts. Allocations and unspent points
persist separately for all seven classes. Version-one rosters migrate without
resetting progression; prior levels receive their unspent point allowance.
The client uses the native paused world branch so animations stop while render
bookkeeping continues with zero elapsed time. Both classic and HD pause flags
are scoped and restored after each call. World time excludes whole 40-ms ticks
spent choosing upgrades, preserving the shared server/client tick boundaries;
resuming may advance less than one extra tick beyond the held render time.
Removing a fractional number of ticks instead causes recurring position
corrections when native reconciliation copies server positions.
The input/network clock
continues so skill and attribute requests can be processed. The world-time
mapping persists after returning to town and resets with the native game clock,
avoiding a catch-up burst. Upgrade controls sit
on the right, with the active Survivors stat controls on the left.

Waves arrive every four seconds, increasing with time and participant count.
Wave spawns cap at 128 living enemies. Every eighth wave has a tougher monster;
Diablo appears after nine minutes. Kill him and all remaining enemies before
ten minutes to win. Timeout or no ready living participant ends the run.
Monster level starts at the initiating Survivors class level +5. Their native
HP scales with time, passive regeneration is disabled, and resistances cap at
75% before sunder calculations. Only tougher enemies and the boss retain drops.

One shared run is allowed per game. Other players can choose their own class
at Charsi to join it. Unregistered normal heroes cannot enter through its red
portal. **Return to town** restores the normal character; approach Charsi to
choose a class again if the shared run is still available. Completion restores
every participant and closes the portals. There is no permanent stat reward or
dice-event chain for Survivors. The event automap reveal, monster markers,
timer and terror-zone exclusions remain scoped to the arena.

## Setup and verification

The existing sendable `hud_cube_map` state is required; see
[cube maps](cube-maps.md). No additional stats or skill rows are required.
The startup helper remains necessary for the existing extended-state patches.

If the class menu opens but entry reports that the mod needs the sendable
`hud_cube_map` state, native compatibility has passed; check the active mod's
state table. With `-mod RMD-MP -txt`, the source belongs at
`Mods/RMD-MP/RMD-MP.mpq/data/global/excel/states.txt` under that player's D2R
installation. The working RMD-MP table has 260 data rows and `hud_cube_map` at
ID 259, with `aura=1` and `nosend` blank. Use the complete matching state table
on every participant's installation, then fully restart through the startup
helper with `-txt` to regenerate the compiled table. The two-row file in
`examples/cube-maps` is a merge example, not a replacement for the full table.

The reference executable for Survivors is the supplied `Downloads/D2R.exe`,
SHA-256 `e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8`
(file version 1.2.69270). Regenerate its entry checks with
`python tests/generate-survivors-native.py <path-to-D2R.exe>`. The earlier
`2c2ddb60a40ede3aaff51afb64e932621a03e32487687b6c64d88205fa652360`
executable has identical Survivors entries and remains compatible. Their only
differences are six bytes in allocation constants outside these checks. The
generator reads executables without modifying them. Runtime checks remain
enabled: a mismatch at `D2R.exe+0x28f2b0` cannot be explained by these two disk
variants, since that save-dispatch entry is identical in both. Diagnose the
running process and startup hooks if the mismatch persists.

Survivors supports the launcher's **Customize Save File Limit** setting at
either the original 8 KiB or 32 KiB. The latter changes the bytes at
`28f2b7`, `28f2d1`, `28f2f9`, `28f324`, `28f454`, `28f47f`, `28f487`
and `28f496` from `20` to `80`: stack allocation/release, cookie and saved
register slots, serialization capacity, and saved-buffer length validation.
The native calling convention and the normal-hero save wrapper remain the same.
For this routine the full 599-byte function, including chained fragments, must
match one of the two exact generated hashes. Partial edits, other sizes and
unrelated code changes are rejected. HUD preserves the selected limit and does
not apply the save-limit edit itself or change executable files.

The startup helper applies its memory plan after HUD initialization. A report
captured before that plan can therefore show the original entry even when the
configured limit is 32 KiB. Other launch paths can apply memory edits before HUD;
both complete variants are supported at installation.

On a Survivors signature failure, `%LOCALAPPDATA%/D2RHUD/feature-status.txt`
includes the exact bytes captured by the failed check, the expected/observed
hashes, and any memory-read error. This capture happens before the Survivors
hooks are installed. Read-only diagnostics follow up to four common entry jumps,
report their destination modules or private allocations, and list loaded module
paths. A destination identifies where a jump leads, not necessarily which
component installed it. These diagnostics do not change code or bypass checks.

The fixed-size custom packet transport uses Survivors protocol version 5.
Native entry signatures gate the feature on supported 1.2.69270 code. The tests
exercise learned-skill packets, Fire Ball's actual native action/projectile
creation and triggered-effect packets, including matching target coordinates,
movement/target restoration and player assignment with preserved save buffers.
Wrapper fixtures cover seven-class
isolation, normal saves/previews/shared-stash serialization, death, disconnect,
failed creation, profile validation and persistence. UI fixtures cover 720p/4K.
The user confirmed level-one Fire Ball, the interactive skill/stat controls,
and smooth Sorceress movement after the resume timing correction.
Native fixtures execute both classic and HD zero-delta paused rendering, actual
native resume/town world updates without catch-up, repeated pause/game-clock
reset cases, aligned reconciliation across two non-integral pause durations,
and attribute
spending without world-frame progress. Host command fixtures cover single/bulk
allocation, replay/identity/budget checks and derived life/mana gains. UI tests
click the actual stat buttons at 720p/4K, including modifier keys, disabled
pre-upgrade/empty-budget states and the absence of a full-screen dim layer.
Saved class levels, XP, ranks and allocations remain compatible. Other classes and Save &
Exit/reload also need live verification beyond the native fixtures.
Isolation regressions check distinct XP in all seven save slots, selected-class
entry packets, native avatar switches on the same normal hero, and no XP awards
to inactive builds retained by an existing run.
