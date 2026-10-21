# Summon persistence native audit

Audited executable: `D2R.exe`, SHA-256
`2c2ddb60a40ede3aaff51afb64e932621a03e32487687b6c64d88205fa652360`.
All addresses below are RVAs in this executable. The generator
`tests/generate-summon-persistence-native.py` rejects other executable hashes and
regenerates the entry signatures used to gate feature installation. Signatures
cover the directly invoked native routines, including the supported summon skill
implementations. They are compatibility checks, not a substitute for an audit of
a new executable.

## Ownership and creation

`0x29b4c0` corresponds to the native summon-pet factory. Its ABI is
`Unit*(Game*, SummonArg*)`. The argument is 0x30 bytes: flags uint32 at 0;
owner pointer at 8; monster int16 at 0x10; extra int16 at 0x12; AI special state
int32 at 0x14; animation mode int32 at 0x18; X/Y int32 at 0x1c/0x20; pet type
int32 at 0x24; maximum int32 at 0x28. Flag 1 means explicit coordinates. The
factory calls `0x29b320`, which passes type and maximum to `0x2df600` for native
pet registration. It then assigns the owner, enables AI, and schedules AI think.
The factory alone does not initialize skill-specific stats or equipment.

`0x367f60` applies summon passive stats, ABI
`int(Game*, owner, pet, skillId, skillLevel, itemLevel)`. This identifies the
source skill without hard-coding class or custom monster IDs. Creation is tracked
even while the option is disabled, allowing an army created before the option is
enabled to be saved later.

Player data +0x98 points to a player-pet structure whose first pointer addresses
the pet-type array. Each type occupies 0x10 bytes: head pointer at 0, current
count at 8, maximum count at 0xc. Each pet node has flags at 0, GUID at 4, pet
information at 8, and next pointer at 0x18. Flag 1 marks a dead/dormant pet. The
native pet-type record count is int32 at `0x1c9fbd0`. `0x2df740` searches the
same layout for a pet GUID and returns its type.

## Skill records and restoration

The shared character loader `0x2934b0` and writer `0x318c80` have independent
32-byte entry signature checks. CubeMap installs their existing wrappers when
these two checks pass, independently of the broader Survivors avatar/skill
checks. SummonPersistence retains its own summon-routine checks. No signature
failure is ignored: modified load/save entries are not hooked, and unrelated
Survivors failures keep Survivors disabled. The mapped-executable regression
mutates both required entries and an unrelated Survivors entry, tests actual
MinHook installation and cleanup, and verifies persistence can install only
when its required entries match.

The skills table pointer/count are `0x1c9fad0`/`0x1c9fad8`, stride 0x284.
The compiled `srvdofunc` int16 is at +0x4e, confirmed by the loader's schema
construction at `0x1d74e7` and `0x1d7639`. Summon monster int16 is +0xdc,
pet type int8 +0xde, maximum formula uint32 +0xe0, and calc1 through calc4
uint32 formulas +0x158/+0x15c/+0x160/+0x164. Blank formulas use the native
out-of-range sentinel and evaluate to zero.

The native server-do function table starts at `0x14bf500`, eight-byte entries:
16 `0x371690`; 31 `0x368cb0`; 49 `0x359950`; 56 `0x369e90`;
114 `0x3608d0`; 115 `0x360c20`; 119 `0x361cb0`; 144 `0x382370`.
Iron Golem uses 57 (`0x36a2d0`) and remains under native save/load handling.

`0x21e3a0` resolves summon monster/mode from a source skill. Formula bytecode
pointer/count are `0x1c9eab8`/`0x1c9eac0`; evaluator `0x6d08c0` takes the
bytecode slice, callback `0x2185e0`, function table `0x1800280`, count 7, and
an owner/skill/level context.

Skeleton server-do 31 consumes a corpse before invoking the factory, so rejoin
restoration instead uses the factory and its native initialization sequence:
base stats `0x368a90`; components; passives `0x367f60`; summon resistance
`0x367910`; owner-linked AI update `0x3c5410`. Physical skeleton class 363
uses `0x367d40(owner, pet, corpseType, skill, level)`. Mage class 364 uses
`0x367af0(owner, pet, corpseType, fullSetLevel, shield, special)`, special=1,
which initializes the mage AI as well. Current full-set state 97's stat 351 is
read with `0x1e2e00`. Saved component bytes retain mage element and appearance.
Custom monsters using 31 receive base stats/passives/resistance/AI without the
two stock classes' special component routines.

Server-do 144 creates three adjacent monster classes and rejects towns. Its
post-factory setup is `0x29bf70(pet, skill, level, 1)` plus passive stats and
AI parameter 0 = game frame + evaluated calc1. Rejoin restoration must bypass
the town restriction for permanent custom companions and create exactly one
replacement for each saved unit, rather than recreating three per saved unit.

## HD spawn visibility

Returning companions use animation mode 1 (monster neutral) in the adjusted
factory argument, before native creation. Native factory `0x29b4c0` forwards
argument +0x18 unchanged through both placement branches (`0x29b5c1` and
`0x29b61b`) into the monster creator `0x304580`. No renderer mutation or
per-monster visibility mask is installed by this feature.

Live comparison on 2026-10-17 found the restored Blood Golems' HD model instances
with draw-layer mask 0 and zero active parts; normally cast controls had mask 9
and six parts. Definitions, model assets, ECS entity generations, transforms,
materials and transparency settings were present in both cases. Reapplying the
definition's mask and calling native rebuild `0x8a09c0(ModelInstance*)` on the HD
thread made the affected golem visible, confirmed in game.

The shipped `hd/character/enemy/bloodgolem/animation/combined.timelines` specifies
`SetMeshLayerVisibility(0)` at skill1 time 0 and `(9)` at 0.03333334 seconds;
neutral/walk timelines do not reset that mask. Native event callback `0x8be980`
is registered under FNV-1a hash `0xf54a0f2f` (`SetMeshLayerVisibility`); it writes
payload +0x38 into each child model's mask and invokes `0x8a09c0`. An interrupted
spawn timeline can therefore leave a living, fully loaded model hidden. Starting
returning companions in neutral avoids the spawn-only event entirely. Fresh
casts, excluded temporary summons, and Iron Golem retain their native paths.
After a full game restart with the neutral-restoration build, the user confirmed
that all restored Blood, Clay and Fire Golems were visible in HD. Other custom
models and remote clients still need their own gameplay checks.

## Temporary summons

Unit +0x140 is the unit-event head. Event fields are type byte at 0, flags
uint16 at 2, expiry int32 at 4, and next-for-unit pointer at 0x30. Event 7 is
monster modifier expiry; event 4 is a trap event. Event 12 is ordinary state
expiry, including temporary buffs on permanent companions; it does not make the
companion temporary. Event 7 is also used by other modifier callbacks, so an
event of that type alone must not exclude a permanent companion. Monster
modifier 21 is `TEMPSUMMON` and supplies the explicit temporary-summon signal.

Some finite companions use AI parameters instead of expiry events. The loaded
monstats table pointer/count are `0x1c9f878`/`0x1c9f880`, stride 0x1dc.
Native AI int16 is +0x4e, directly read at `0x31e028`. Finite AIs are Hydra
86, Assassin Sentry 101, Blade Creeper 102, Death Sentry 104, and Raven 107.
Raven has a finite fallback attack count even without a source skill. Checking
the actual summoned monster AI keeps custom permanent skills that reuse 114 or
144 eligible; the skill implementation number alone is insufficient.

The exact executable's 144 routine evaluates calc1 at `0x382604` and writes its
result plus game frame to AI-control +0x3c at `0x3827bc`. This is only a lifetime
when the summoned AI interprets it as such. Custom permanent AIs can ignore it,
and other positive skill formulas can represent health or damage. Therefore the
actual saved monster variant's finite AI, trap event, and explicit temporary
modifier determine exclusion; a positive formula alone does not. The feature
does not remove timers or replenish finite companions. Missing or invalid native
monster records fail closed and are not persisted.

## Evidence and limitations

Disassembly was inspected directly using PE section mapping, x64 exception
function metadata, and Capstone. Upstream D2MOO supplied source correlation for
the factory, player-pet list, skeleton initialization, and AI lifetime semantics;
offsets and callable addresses above were checked against the supplied D2R
executable rather than copied from legacy D2 addresses. Relevant upstream files:
[Skills.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/SKILLS/Skills.cpp),
[PlayerPets.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/PLAYER/PlayerPets.cpp),
[SkillNec.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/SKILLS/SkillNec.cpp),
[AiThink.cpp](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/AI/AiThink.cpp).

The supported server-do families are explicit. An entirely new native summon
implementation needs a separate initialization audit; arbitrary new executable
code cannot be inferred from a custom TXT skill row. Runtime live-game validation
remains necessary for gameplay behavior; fixture tests establish lifecycle,
filtering, count, target, and companion-file behavior without executing D2R.
