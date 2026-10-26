# D2RHUD for D2R 2.4

A HUD and mod extension for Diablo II: Resurrected 2.4, adapted from
[D2RHud](https://github.com/locbones/D2RHud) with support for modded TCP/IP games.
Native features check executable compatibility and may require matching mod data
on the host and clients.

## Features

- One HUD menu with grouped options, search, a Sanctuary theme, and remembered
  window sizes and pane layouts.
- Editable HUD and quest translations for all 13 game language codes, an offline
  translation editor, and bundled Unicode font support. See
  [translation instructions](translations/README.md).
- Floating damage and DPS, buff icons, item stat ranges, loot filtering, and a
  grail tracker.
- **Open HUD Menu** in the game's Controls settings, with F12 as the default.
- Optional in-game widescreen bars removal, D3-style Whirlwind with belt potions,
  persistent summons, and stack splitting/socketing.
- Mod integrations for cube maps and events, quest tracking, rune storage,
  item visuals, and immunity sunder.
- Temporary RMD-MP Candy Corn migration: legacy `Ev11` items gain quantity 1 on
  character load, with an original-save backup and an on-screen notice.

Some integrations are specific to RMD-MP. Experimental Workshop features default
to off and need the corresponding mod content.

## Build

Use Visual Studio 2022 with Desktop development with C++, the v143 toolset,
a Windows SDK, and vcpkg MSBuild integration enabled.
Dependencies are declared in [d2rhud/vcpkg.json](d2rhud/vcpkg.json): CascLib,
Detours, MinHook, ImGui (Win32/DX12 bindings), Lua, and sol2. Restore them locally;
installed dependency trees are excluded from Git.

Open [d2rhud.sln](d2rhud.sln) and build **Release / x64**, or run from an x64
Visual Studio developer prompt:

```bat
msbuild d2rhud.sln /m /p:Configuration=Release /p:Platform=x64
d2rhud-injector\build-startup.cmd x64\Release
```

The second command builds the startup helper, `Pandemonium.HudLoader.exe`.
Build output stays in local output folders.

## Install and run

With D2R closed, place the matching `d2rhud.dll` and
`Pandemonium.HudLoader.exe` in the game installation. The loot filter engine is
provided as [lootfilter.lua](lootfilter.lua); place it in the game working
directory when using loot filtering. Matching mod tables and optional assets
must be supplied by the mod or its launcher.

Run the startup helper with the D2R directory as its working directory and pass
the usual game arguments, for example:

```bat
Pandemonium.HudLoader.exe -mod RMD-MP -txt
```

Use `-txt` when compiling mod source tables. Normal BIN-based launches do not
need it. The startup helper is required for features that extend the game's
startup table limits; the older attach-only injector runs too late for those
features. See [startup instructions](d2rhud-injector/STARTUP.md).

Press **F12** to open the HUD, or change **Open HUD Menu** under the game's
**Options > Controls**. Settings save per mod or character as appropriate.
Installation diagnostics are written to
`%LOCALAPPDATA%\D2RHUD\feature-status.txt`.
Startup errors name the failed feature and its reason when using the matching
updated helper and DLL. Optional Act VI data issues do not block five-act BIN
games, including a saved Act VI preference that cannot activate with those BINs.
Six-act packages and requested TXT preparation still require valid Act VI data.
Act VI validation accepts both
GitHub LF and Windows CRLF line endings while checking file contents.

Quest recipe annotations in `data/D2RHUD/table-metadata.json` do not depend on
the checksum or total size of `cubemain.bin`. Ordinary recipe updates leave
quests available; the matcher checks the quest gates it uses against the
loaded native recipes. Existing quest progress does not need to be reset.

## Source and publishing

- `d2rhud/`: HUD source, embedded fonts/art, and dependency manifest.
- `d2rhud-injector/`: injector and startup helper source.
- `lootfilter.lua` and `definitions.lua`: loot filter engine and editor definitions.
- `mod/`, `content/`, and `examples/`: mod data, authoring content, and examples.
- `tests/` and `tools/`: regression tests and development/mod-authoring utilities.
- `x64/Release/`: the current DLL, startup helper, and Lua engine for launcher downloads.

Restored dependencies, debug/intermediate builds, release archives, backups,
player data, and local AI/internal documentation are excluded by
[.gitignore](.gitignore). Only `d2rhud.dll`, `Pandemonium.HudLoader.exe`, and
`lootfilter.lua` are allowed in `x64/Release`, preserving existing launcher
downloads. Keep the published Lua copy in sync with the root source when
preparing an update. Other distribution archives belong in release assets.
See [tests/README.md](tests/README.md) for verification commands.

## Credits

- Original HUD: [locbones/D2RHud](https://github.com/locbones/D2RHud).
- scizzydo for resize logic.
- Killshot for chat and detour structures.
- [dschu012](https://github.com/dschu012) for D2R base configuration.

![D2RHUD Example](D2RHud_Example.png)
