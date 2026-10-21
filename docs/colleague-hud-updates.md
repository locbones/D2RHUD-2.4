# Colleague HUD updates

Source: `D2RHUD-combined-patch-source.zip`, SHA-256
`bc8bc2c84982b5447ef2d5372d0f193b6cdc13a4e58546e98d8019f7e5902250`.

The archive is an additive patch for an older HUD binary. Its behavior is
integrated into this project's normal source build. The bundled original and
reference DLLs, their import-table offsets, and the binary reconstruction
scripts are not build inputs for this repository.

## Included behavior

- Eligible selected auras can occupy the left skill slot and use the native
  activation, periodic updates and cleanup paths.
- A selected aura in one slot disables aura choices in the other slot. The
  picker displays unavailable choices in red and rejects clicks; the server
  assignment guard also covers hotkeys and restores the authoritative slot.
  Replacing an aura within the same slot remains allowed.
- Existing saves with both slots assigned to auras retain right-slot priority.
  Item-granted aura effects do not reserve a selected-skill slot.
- Uber monsters can display the native immunity labels and monster modifiers.
  These changes remove display exclusions without editing monster resistance,
  combat, localized strings or modifier definitions.
- The previously integrated linear sunder and dynamic immunity-label behavior
  remains in `Sunder.h`, `D2RHUD.cpp` and `ImmunityLabels.cpp`. It is not applied
  a second time. The HUD's configurable resistance floor and total/base-stat
  handling are retained.

Selected-aura behavior is provided by `SelectedAuras.cpp`; the two Uber display
branches are integrated into the existing `ImmunityLabels` installer. Both
features verify native instructions before changing code. Their cleanup paths
restore owned patches. The selected-aura module remains loaded until process
exit so any later hook chaining through its relay cannot reach unloaded helper
code. Eligible aura table flags are enabled in
memory after the compiled skill table is available. No installed `skills.txt`,
game executable or character save is rewritten by these runtime features.

The supported executable is the project's audited D2R `3.1.1.0 (91923)`, SHA-256
`2c2ddb60a40ede3aaff51afb64e932621a03e32487687b6c64d88205fa652360`.
The archive names an older executable with a different digest. Its native
instruction guards were compared against this project's executable before
porting; compatibility is established by those checks rather than the archive's
version label.

## Optional mod-table utility

The archive's Paladin table utility is available as:

```bat
python tools\paladin_leftskill.py --skills <input-skills.txt> --out <new-output-skills.txt>
```

It preserves the input file, right-slot eligibility, other cells, BOM and line
endings. It refuses to overwrite its input or an existing output. The tool is
optional because the runtime feature already enables eligible left-aura flags.
It is not run automatically against installed mods.

## Verification

From an x64 Visual Studio developer prompt, use the existing static dependency
directory containing `include` and `lib`:

```bat
tests\run-selected-auras.cmd <output-directory>
tests\run-selected-auras.cmd <output-directory> <D2R.exe>
tests\run-sunder.cmd <dependency-root> <output-directory> <D2R.exe>
python tests\paladin-leftskill-test.py
```

The aura fixtures exercise selection, activation/cleanup decisions, periodic
levels, picker and hotkey guards, item-aura exclusions and table flag ownership.
The immunity fixtures include repeated sunder refreshes, changing resistance
bonuses, native label selection, all Uber guard-byte mismatches, protection
failure rollback and reversible branch patches. With the executable argument,
the aura fixture also installs and removes all six patches in a private mapped
image and checks native guards and generated call/continuation targets. It does
not execute game code.

Native inspection and these fixtures do not replace live visual/gameplay
checks. In game, select an aura on the left and confirm activation; change and
remove it; try opposing-slot picker and hotkey selection; verify item-granted
auras still work; inspect Uber immunities/modifiers before and after resistance
changes. Persistent summons, D3-style Whirlwind and the Options menu layout are
preserved by this integration.


## Gold / Terror Zone / Assassin Whirlwind archive

`D2RHUD-gold-tz-whirlwind-source.zip` is also integrated. Its auto-gold and
elapsed-period Terror Zone rotation behavior was already present. Its remaining
ordinary Assassin Whirlwind restart helper is now included unchanged in
`plugin/D3WhirlwindRestart.h` and invoked by the existing client start hook.
See [Whirlwind integration and native tests](d3-whirlwind.md#colleague-assassin-restart-integration).
This source build retains the current map events, persistent summons and other
HUD work rather than using the archive's older compiled DLL.
