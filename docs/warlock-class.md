# Warlock class prototype (D2R 2.4)

This opt-in implementation adds Warlock at player class ID **7** on the audited
1.2.69270 executable. It retains the original seven class IDs. It is a class
backport; the 3.1 Warlock skill functions are not implemented.

## Startup and mod data

The HUD must load through the startup helper, before skill/class tables compile.
The helper's readiness gate now also waits for class installation. An enabled
manifest with invalid tables or an unsupported executable rejects startup.
Mods without the manifest retain normal class behavior.

Place `data/Pandemonium/extended_classes.json` inside the mod:

```json
{ "version": 1, "warlock": true }
```

`playerclass.txt`, `plrtype.txt`, and `charstats.txt` must preserve their seven
existing class records and append Warlock as the eighth compiled record. The
prototype uses `war`, the `WK` token, the archived model/skeleton/animations,
native Attack and common actions. Warlock currently uses the Necromancer's
inventory grid. The native experience lookup retains its existing fallback to
the Amazon column for class IDs above six; an independent Warlock experience
column is not enabled by this prototype.

Warlock currently uses the Necromancer's native player audio/foley profile.
The original seven profiles are preserved. The client otherwise reads a null
eighth entry when an animation emits a footstep, causing a crash even on the
creation screen. Warlock-specific audio and its sound-table mappings are not
yet ported.

The native skill panel's seven-entry class-label arrays are guarded for the
eighth class. Warlock skips these unavailable title/tab labels; no player class
ID or original class layout is substituted. Skill mechanics and a dedicated
Warlock skill-panel layout remain deferred.

The legacy animation residency flags are relocated to a writable eight-class
array (40 DWORDs per class). Both the updater and initializer use the relocated
storage, and the initializer clears all eight classes. The original array was
immediately followed by the COF cache: class 7 / mode 6 overwrote its first
linked-list head, causing Save & Exit to crash during cache cleanup.

The patched native paths include both class registries (reflection and character
creation), frontend empty-preview sentinels, localized class names, model lookup
and preloading, skill-cache allocation, player creation/cloning, and class-skill
save sections, including the separate current-format D2R header validator and
new/saved player allocation paths. These patches contain executable signatures and instruction-byte
checks, and install as a transaction. Native registry nodes use the game's own
allocator and tree rebalancer.

## Isolated assets

From the repository root, stage a separate mod with its own save path:

```text
python tests/stage-warlock-test.py OLD_EXTRACTED_DATA NEW_EXTRACTED_DATA GAME_DIR
```

Use the extracted 2.4.1.2 (69270) and 3.1.1.0 (91923) archives respectively.
The script refuses to overwrite an existing `HUD-WarlockTest` mod. It does not
modify RMD-MP. Do not replace a mod's entire inventory or experience table with
the newer tables: their layouts and row positions differ.

The TCP-enabled test runtime also requires `hd/global/excel/desecratedzones.json`.
Staging copies it verbatim from `Mods/TCP/TCP.mpq/data` in the game directory
and rejects a missing source, rather than creating a test mod that will fail
when the runtime initializes zones.

The asset compatibility pass removes newer Warlock rendering components and
unsupported animation events, and excludes the deferred Warlock skill states.
It preserves the supported archived animation bindings and clip timing. Spell
effects and the newer hand/book effects are intentionally deferred with skills.
The English name/description and their archived translations are included.

Creation-only animation states use their unarmed clips for empty weapon
bindings, including both idle fidgets. Both class definitions preload their
state machine so an initial animation request can be resolved when the preview
becomes ready. The original head models and hair materials are preserved.
The staging script uses
`GAME_DIR/tools/D2RModelConverter/resources/native/granny_inspect.exe` to audit
all model LODs for texture references, including strings in shader ExtendedData.
It copies shared dependencies absent from 2.4, including the two Warlock hair
reflectance lookup textures. These are outside the Warlock asset folder and
are required in addition to the hair albedo textures. To update these assets
in an existing test mod:

```text
python tests/warlock_assets.py OLD_EXTRACTED_DATA NEW_EXTRACTED_DATA MOD_DATA_DIR --inspect-tool PATH_TO_GRANNY_INSPECT_EXE
```

Restart the test game after changing cached models or animation metadata.

Pandemonium normally updates the installed HUD from its configured release.
A local prototype can therefore be replaced on launch. Confirm the actual
loaded DLL when testing; an eighth layout widget alone can fall back to Amazon
with a build that does not contain the class support.

## Validation

Run in a VS x64 Native Tools prompt:

```text
tests\run-extended-classes.cmd OUTPUT_DIR GAME_DIR\D2R.exe
```

The fixture executes native inventory lookup, class parsing/formatting, frontend
sentinel instructions, player-allocation guards, creation-registry initialization
and insertion, and skill-cache allocation/counting code from a private PE image.
It checks that invalid class 8 remains rejected and allocation guards remain
intact. Startup-gate, extended-state, and cube-map regression tests cover shared
startup dependencies. These tests do not replace an in-game create/play/save/
reload check or visual verification of the backported assets.

An optional third argument to `run-extended-classes.cmd` supplies a Warlock
`.d2s` file for read-only reproduction. The fixture runs its original checksum
and header through the real native D2R validator before and after patching:
the unpatched reader reports `SYSERROR_EXPANSIONGAME`, and the patched reader
accepts the class. It also checks classes 0–6, rejection of class 8/255,
expansion flags and checksum errors.
The audio fixture also executes the native table lookup that caused the
class-7 null dereference and verifies the replacement profile.
The presentation fixture executes each guarded label-indexing path, preserving
classes 0–6 and taking the safe branch for class 7 without reading the panel
arrays beyond their bounds.
The residency fixture reproduces that cache-head overwrite with the original
native instructions, then exercises all 320 relocated cells and verifies the
adjacent cache is unchanged.
The skill-section fixture executes the native header writer and reader branch.
Warlock's header records its actual class skill count instead of the stock
maximum of 30. Earlier class-only prototype saves declared 30 bytes but wrote
none; the reader recognizes only class 7 with zero compiled class skills, a
30-byte declaration, and an immediate `JM` item marker, and corrects its local
cursor count. It does not edit the saved file or bypass checksum validation.
Original classes, real skill data and truncated sections retain their normal
reader behavior.

The isolated test's name/description, unselected creation-screen animation,
hair rendering and gameplay entry have been confirmed. The saved `warlo`
character retains class ID 7 and a valid checksum after gameplay. That test
ended with a confirmed animation-cache overflow on Save & Exit, now covered by
a native regression test and corrected in the current build. Reload then
exposed the skill-header/body length mismatch, also reproduced and corrected
in native tests. Save & Exit/reload now works in the isolated test, confirmed
by the player and a successful native saved-character loader return (0).
Integration into RMD-MP and a durable Pandemonium launch configuration remain
separate from this isolated validation.
