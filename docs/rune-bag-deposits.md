# Mixed rune storage-bag deposits

Put exactly one **Z01 Storage Bag** and one or more runes into the cube, then
Transmute. Runes may be mixed in any combination that fits the cube. HUD totals
each rune's contributions to `RB_Low`, `RB_Mid`, `RB_High` and `RB_Ultra`, consumes
all of those rune items through the native cube transaction and returns the
existing bag through **useitem**. Existing balances, other stored currencies,
item identity and saved item modifiers are retained. No new recipe combinations
are needed. There is no additional option to enable.

The helper matches the exact case-sensitive item code `Z01`; the unrelated
lowercase `z01` Enhance Crystal does not qualify. A second bag, any non-rune item,
a bag alone or runes alone goes through normal recipe matching. Withdrawals,
essences, gems, map recipes and other combinations retain their existing paths.
This exact bag-and-runes combination takes priority over other recipes for that
same combination, including the existing conditional Thul quest recipe.

## Values and capacity

Values come from the **loaded single-rune deposit recipes**, with `RB-Low`,
`RB-Mid`, `RB-High` and `RB-Ultra` resolved against the loaded properties and stat
tables. Keep those single-rune rows as the definitions; quantity-specific rows
may remain but are unnecessary for this helper. Changing a rune's single deposit
value changes mixed deposits after the game loads the updated tables.

The current mod defines El through Amn as 1 through 1024 low points, Sol through
Um as 1 through 1024 mid points, Mal through Zod as 1 through 1024 high points,
and Di/Ab/Er as 1/2/4 ultra points. One of each of the 36 runes totals **2047 low,
2047 mid, 2047 high and 7 ultra**. Custom runes inheriting the native `rune` item
type also work when they have an eligible single deposit row. One such row may
grant fixed positive values to multiple tiers. Stackable runes contribute their
full native quantity; the rune stack is consumed as a whole.

Definitions must be enabled, eligible for the current difficulty/version,
unrestricted by class/ladder, use exact bag/rune item codes and plain `useitem`,
and grant only these four fixed positive properties. The existing upper-bound
operation (16) is supported; quest-specific operations are excluded. Ambiguous
conflicting definitions, missing definitions and random-value deposits refuse
rather than guessing. Loaded save/send widths, native signed integer limits and
existing single-deposit upper bounds constrain the final balances. If any tier
would exceed capacity, the entire deposit is refused before consumption.

All four saved-stat updates are checked before calling the native output
routine. A refused inventory preflight restores the original balances. Native
useitem chooses and retains the original bag while consuming the other cube
items. Full item publication refreshes the bag's displayed/saved stats. This uses
existing item stats and serialization; there is no new save file format, extra
companion file or permanent character-stat modification.

## Installation and verification

Fully close D2R before replacing the DLL, then relaunch. No mod table edit is
required for this update. In multiplayer, the game host must have this HUD build.
The native matcher/output hooks are guarded for the project's supported
executable; `D2R.exe` is not edited on disk.

For a local/custom DLL, enable the launcher's existing **Disable update checks**
setting before launching. Pandemonium's HUD updater otherwise compares the DLL
with the public release and replaces a different local build. The October 18
startup investigation confirmed this replacement in the launcher activity log;
the resulting crashes loaded the original October 8 DLL, not this build. Verify
the installed DLL against the package's SHA256 if features disappear after a
launch. No launcher binary changes are needed for this setting.

From an x64 Visual Studio developer prompt:

```bat
tests\run-cube-visual.cmd <output-directory> "<D2R.exe>" "<exported global\excel directory>"
```

The optional table directory loads the actual exported `weapons.bin`,
`armor.bin`, `misc.bin`, `cubemain.bin`, `properties.bin`, `itemstatcost.bin` and
`itemtypes.bin` fixtures. Tests cover all 36 shipped rune values, inherited/custom
multi-stat runes, same-type and mixed deposits, stack quantities, totals beyond
16-bit modifier limits, capacity/input bounds, exact item-code scope and
save/placement rollback. The real native cube output routine is executed in a
private mapped image: it retains the original bag/GUID, consumes exactly the
mixed rune inputs and publishes the updated bag. Actual native stat insertion
and item serialization verify all four balances. Inventory/network boundaries
are test stubs; no running game is changed by the tests.

For in-game acceptance, note the bag's starting balances and unrelated stored
currencies, deposit a few different rune tiers, and check the exact additions.
Save, exit and reload to confirm the new balances persist. Test a nearly full
balance to confirm the runes remain when the whole deposit cannot fit.
