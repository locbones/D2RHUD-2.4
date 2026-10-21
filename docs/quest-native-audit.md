# Quest cube native adapter audit

Target: user-supplied `C:\Users\djsch\Downloads\D2R.exe`, SHA-256
`e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8`.
No executable or running game was modified. Native execution below uses a private
PE section mapping inside a disposable test executable, with engine inventory,
clock and CRT boundaries stubbed.

## Evidence and scope

- **QNA-1 (shipped-artifact disassembly):** Microsoft VS 18 dumpbin 14.50.35724,
  `/DISASM:BYTES /RANGE:0x140308840,0x14030906c`; native recipe scanner plus its
  switch table. FNV-1a over RVA `0x308840`, size `0x82c` is
  `0xb01c7cc2c185785f`.
- **QNA-2 (shipped-artifact disassembly):** dumpbin native input-slot matcher at
  RVA `0x305d00`; code plus switch table size `0xb4c`, FNV-1a
  `0x0cd622517e6e82e9`.
- **QNA-3 (controlled replay):** `tests/quest-cube-native-test.inl`, function
  `QuestCubeNativeTest::Native`, executes the complete isolated scanner and the
  original native lower input matcher from the target. It also calls the public
  journal-aware adapter against synthetic journal states and recipe tables.
- **QNA-4 (shipped-artifact disassembly and controlled replay):** native output
  creation RVA `0x306c50` through `0x30883c`, with specific call sites below.
  `QuestCubeOutputTest::Native` exercises actual creation orchestration and
  placement/consumption paths through `CreateQuest`.
- **QNA-5 (shipped-artifact disassembly plus existing controlled replay):** native
  output parser RVA `0x2616b0`, unique/set name lookup paths; exercised by the
  existing `tests/cube-visual-test.cpp` private-image parser test.

Audit command outputs and independent fixture executables were retained under
`%TEMP%\d2rhud-quest-native-audit-20261019` during implementation. These are local
audit labels, not REA Evidence IDs. REA/Ghidra admission verified the same target
identity, but its cold-start analysis exceeded both transport and provider
deadlines. No Ghidra function result or Evidence ID was returned; the owned
session was closed. Claims here come from the identified disassembly and native
test execution, not from a successful decompiler result.

## Selection boundary

The higher scanner signature is:

```cpp
struct Match { int row; bool matched; unsigned char padding[3]; };
Match* Scanner(Match*, Game*, Unit* player, Input outputs[3]);
```

The lower predicate signature is:

```cpp
int MatchSlot(Game*, Inventory*, Recipe*, int slot,
              Input outputs[3], uint32_t used[48]);
```

Observed higher predicates include enabled/debug, expansion/version,
ladder/server flags, minimum difficulty, character class, exact cube item-unit
count, time predicates and player-stat operations 1 through 14. It invokes the
lower predicate for all seven recipe input slots, sharing a 48-entry consumption
bitmap and three selected-output records. Lower predicates retain item code,
type, quality, unique/set index, sockets/ethereality, quantity, useitem selection,
and item-stat operations 15 through 28. The lower matcher applies item-stat
operations to the first input only.

Consequently, replacing the whole native matcher with just a code/count test
would bypass unrelated mod requirements. Reusing only the lower matcher also
loses player-stat and time requirements.

`QuestCubeNative.inl` instead copies the signature-bound higher scanner into
invocation-owned executable memory, relocates its complete 34-entry inventory of
external relative references, and redirects only its recipe-pointer/count cells
and switch-base references to private data. All ordinary recipe row ordering and
native predicates remain intact. Each invocation owns a copy of the compiled
recipe table. Ineligible quest copies receive impossible input count 255; eligible
milestone copies have their operation cleared **only when its parameter is the
character-progress stat 398**. Event mode excludes all objective/proof and
ordinary claim rows. Eligible milestone copies shift the real Quill/choice
slots over the removed proof slot and reduce the exact input count from four
to three. The unchanged source recipe is never published as a writable copy.
Other native crafting predicates remain intact.

No live table pointers, compiled recipe records, or item stats are changed. Native
input selection itself normalizes malformed item levels below 1, as the original
scanner already does. Tests use valid levels and verify no such change.

The clone includes registered Windows unwind metadata matching its verified
prologue. Code becomes execute/read after preparation; its pointer/count data
remain separate. Allocations and function-table registrations are released after
the synchronous match. Initialization must capture the verified original bytes
before the existing matcher detour is installed.

The older objective/turn-in adapter paths remain exercised as transaction
fixtures, but the deployed version-2 journal uses event objectives and exposes
no proof recovery command.

## Controlled replay coverage

The fixture verifies:

- transferred/irrelevant item stat 398 cannot deny an eligible journal stage;
- objective versus turn-in state and completed/out-of-order stages;
- a blocked earlier quest row does not shadow a later ordinary bag recipe;
- native debug-enabled recipes cannot bypass stage exclusion;
- higher player-stat progress op 6 is bypassed only for parameter 398;
- unrelated player-stat op 3 / parameter 210 and item-stat op 18 / parameter 474
  remain mandatory;
- ordinary relocated player/base/total stat switch branches;
- difficulty, class, expansion and ladder gates;
- item code, quality, inventory page, quantity, distinct input consumption and
  original `useitem` selection;
- source/native metadata drift and signature changes fail closed;
- source recipe bytes and valid item levels remain unchanged;
- clone addresses resolve through Windows runtime unwind tables.

This is controlled native replay, not a live multiplayer/game acceptance test.
The fixture stubs clock conversion and does not independently exercise every
calendar branch, although those original instructions and switch entries remain
unchanged. Failed memory allocation or executable mismatch refuses managed cube
selection safely.

## Output completion boundary

Native creation returns no reliable success boolean. Observed call sites:

| Operation | Native call | Caller return RVA |
| --- | --- | --- |
| Initial inventory preflight | `0x2dba00` | `0x306db3` |
| Destruction of consumed cube input | `0x2f70a0` | `0x307fe8` |
| Declared output placement, count 1 | `0x2d5e10` | `0x30812a` |
| Extra quantity output placement, count 1 | `0x2d5e10` | `0x30850e` |
| Failed-generation input restoration batch | `0x2d5e10` | `0x3085d2` |

A successful first placement does not establish completion of a multi-output
quest recipe. Every supported declared output and quantity must be observed;
the restoration batch must not inflate the output count. Ordinary successful
creation bypasses that restoration call entirely (success jumps from
`0x30859f` into the epilogue). Any partial
consumption/placement followed by failure is ambiguous rather than a safe retry.
The journal integration must retain its transaction in that case and block a
second reward claim. Input GUID, ownership/page and quantity snapshots can
separate a no-mutation refusal from consumed-input failure without hooking the
generic native destruction routine. Native output types with unknown placement
semantics should be rejected before consuming quest inputs.

The integrated private-image fixture confirms complete two-output and
three-output quest recipes commit; one failed placement or missing generated
output is ambiguous; a refused initial preflight leaves all original inputs
intact and rejects; and loss of an original input before any output callback
is ambiguous. A rejected journal `Begin` never calls native creation.

Proof recovery additionally executes native destination selection
`0x2d5890` through its inventory branch. A valid destination produces one proof
and one full-item notification, with no reward/progress callback. Full storage,
refused placement and failed item generation clean up or leave no new item.
Full signatures measured for the added recovery boundaries are:
`0x2d5890 / 0x15a / 0xb0f96ee700803344`,
`0x2f58c0 / 0x980 / 0x5d3da95b188dffa5`, and
`0x2f70a0 / 0xd0 / 0xec62a147c9ab56ec`.

## Name linker addresses

The unique-name parser loads linker **RVA `0x1c9fc68`**, calls lookup
`0x6d0470`, and indexes table `0x1c9fc70` with stride `0x150`. Set names use
linker `0x1c9fc48`, table `0x1c9fc50`, stride `0x1bc`. Compiled recipe special
indices are native index plus one. RVA `0x1c9fc88` was not established as a unique
name linker and must not be substituted.

## Gameplay event audit (QNA6)

Universal monster death is void(Game*, Unit* victim, Unit* attacker, int petKill)
at RVA 0x328000. It distributes XP through 0x32bba0, performs the death
transition through 0x32fd20 and invokes built-in quest kill callback 0x2a1a80.
Loot hooks are unsuitable counters: Find Item also reaches them. Old stat210
comes from the mod's item-dependent KillCountRegister skill, not universal kills.

QuestKillEvents validates complete function signatures and captures the victim
before native execution; only a confirmed death transition or synchronous corpse
removal receives credit. It excludes already dead/revived, nonkillable and
friendly victims. Pet attribution uses monster data+0x30 -> AI control, then
AI+0x58 game, +0x60 owner GUID, +0x64 owner type. Each owner must resolve through
the server lookup in the same game; cycles and chains over eight are refused.

The private-image kill fixture executes the actual shipped death body and passes
player/pet attribution, foreign/cyclic ownership, failed transition, duplicate
calls, revived/dead/friendly/nonkillable exclusion and immediate corpse removal.
Full signatures: 0x328000/0x268/0xf1a3b1e2eb88c3b1,
0x2c49c0/0x20/0x04d772b7be349296, 0x1e5620/0x72/0xa1e6f0ef87a796f0.
Deduplicate victim GUIDs for their game's lifetime. Unit low seed is mutable;
it is diagnostic data, not a resurrection-safe deduplication key.

Possession objectives can scan owned inventory on server tick. Generic inventory
add callbacks cannot prove a new acquisition: load, swaps and shared stash moves
also add items. Original Quest41 uses custom Gu1 (GoldUp), not currency gld.
Act X Fallen and Goblin - Gold A both drop Gu1. Strict Hell Fallen/Terror Zone
provenance requires drop GUID capture followed by confirmed pickup correlation.

## Noncreating UI observation (QNA7)

Panel manager pointer is RVA 0x1d7c4e8; live child pointer/count are +0x58/+0x60,
and widget name is the C-string pointer at+8. Traverse on UI/render thread with
bounded nodes, cycle checks and guarded reads. Native toggle path 0x595abe reads
panel+0x51, then calls virtual+0x48/+0x50 to update bytes+0x50/+0x51. Close path
0x596d7f clears both before queuing destruction. Require both flags along live
ancestors except the manager root. Manager arrays+0x88/+0x90 and+0xa0/+0xa8 are
pending changes, not proof of a rendered panel.

WidgetFindChild0x576070 recursively searches without creating. FindPanel0x597320
can call0x597c50 to create a missing panel and must not be used solely to observe.
Actual names are CubeRecipesPanel, RemoddedShopPanel (Rift Shop), MercInfoPanel,
RemoddedInfoPanel and CreditsModal. Forward observations through the journal's
per-session command path; client widget facts cannot become server authoritative
by reading UI pointers on a server tick.

## Fixed reward properties (QNA8)

Compiled properties pointer/count are0x1c9ebc0/0x1c9ebc8, stride0x2e; function
bytes+0x18, stat words+0x20. ItemStatCost pointer/count are0x1c9fb78/0x1c9fb80,
stride0x144; ValShift byte+0x14. Modern dispatcher0x24bad0 calls function1
at0x249b90, then0x2499d0 reads the shift and adds to the saved modifier list.

Actual dispatcher signature has ten Win64 arguments: void(int mode, Unit* owner,
Unit* item, void* mods, int index, int propertySet, Property* property, int state,
uint32_t listFlags, Unit* subtype). Property is four int32 fields id,param,min,max.
The earlier eleven-argument candidate failed actual execution and is invalid.

quest-property-native-test.inl executes the shipped dispatcher, function1, shift
and native saved-list addition/full-item propagation, then production
ClaimQuestBonus. Resist25 remains25; HP100/Mana100 become25600. Unsupported
property functions, capacity overflow, duplicate matching charms and noncarried
charms refuse unchanged. Existing native modifier tests separately prove item
serialization. An extended fixture connects item and player extended modifier
lists through their native parent+0x58 chain and confirms actual native setter
propagation into both item and player full stats. This assumes the carried
charm is already correctly attached/active; the fixture does not make an
inactive charm active or execute the entire charm activation subsystem.
