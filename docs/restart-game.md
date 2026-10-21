# Restart Game

The pause menu has a **Restart Game** button above **Save and Exit**. In an
offline session, it saves, takes the normal exit path, and automatically creates
a fresh game with the same character and difficulty. Normal character and stash
loading, companion saves and game initialization run again. Offline layouts use
the same seed rules as manually leaving and entering through character selection.

The restart waits for the previous server client list to be destroyed and for
the character-selection panel to finish refreshing. Native game creation runs
on the window thread, once, through the normal local character loader.
Changing the selected hero cancels it. A dead hardcore character is not restarted.
Timeouts return control to the frontend; they never repeatedly create games.

This version supports offline sessions only. TCP/IP hosts, joining clients and
online sessions are refused. The Restart Game row/button is hidden and disabled
when the pause menu opens in those sessions, including a TCP/IP host with no
other players connected. Returning to offline play restores it. Fully restart D2R after installing the DLL. Compatibility checks
reject unsupported game code; the local feature report includes Restart Game.

## Installation

Replace `d2rhud.dll` with D2R closed. The button also needs the updated pause
layouts in the unpacked mod. The installed RMD-MP layouts and their desktop and
controller theme copies have been updated; RMD source layouts are updated too.
Keep launcher **Disable update checks** enabled when using a local HUD build so
its updater does not replace this DLL with the older public release.

For another unpacked mod, apply the small layout edit while preserving its style:

```bat
python tools\add-restart-game-button.py "<mod.mpq folder>"
```

The script backs up each existing `data/.../PauseLayoutHD.json`, clones its native
Exit button's style, and adds `PanelManager:OpenPanel:D2RHUDRestartGame`. The
registered OpenPanel message carries the command to the HUD; a new custom pause
action is not accepted by the game's layout parser. Running the script again is
safe. No game executable, launcher binary, recipe or save format changes are
required. HD menu layouts include controller support; the legacy-only pause-menu
renderer does not read these layouts.

## Verification

From a Visual Studio x64 developer prompt:

```bat
tests\run-restart-game.cmd <output-directory> "<supported D2R.exe>"
```

Tests verify save/exit ordering, old-server and frontend readiness, duplicate
requests, character identity, offline restrictions and timeout cancellation.
Live investigation verified offline mode 0 and its character list/selection
globals. Mode 1 uses the online list. The native registered OpenPanel dispatch
and command argument are exercised in the executable fixture as well.
The executable test maps a private copy and executes the real native entry
loader for Normal, Nightmare and Hell. It verifies the offline list and selection,
character identity, difficulty, signature rejection and MinHook lifecycle. The
game-creation, profile/path and security-cookie boundaries are test stubs; live
disk saves and complete game creation require an in-game check.

For acceptance, note your experience, inventory, gold and difficulty, click
Restart Game, and verify the same hero enters town with those saved values.
Test twice in succession, then verify ordinary Save and Exit still returns to
character selection. Test persistent summons and shared-stash contents too.
