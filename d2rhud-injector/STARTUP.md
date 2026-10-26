The attach-only injector in main.cpp cannot enable enlarged state tables before
the game's startup assertion. Build the startup helper from a VS x64 developer
prompt with:

    d2rhud-injector\build-startup.cmd x64\Release

Run `Pandemonium.HudLoader.exe` with the D2R installation as its working directory
and pass the usual game arguments, e.g. `-mod RMD-MP -txt`. Pandemonium launchers
can use this helper in place of their bundled helper; the existing binary memory
plan protocol and progress messages are retained. The source is shared with
Pandemonium's `CrossPlatform/native` integration; keep its three native source
files synchronized when updating that launcher.

For HUDs exporting `D2RHUD_States512Startup`, the helper creates only its own new
game suspended, verifies the native state loader, and arms a temporary two-byte
branch at RVA 0x1dc277. Once the loading thread reaches this instruction, mod
globals are available. The helper checks the initial thread and BIN-loading
workers, suspends the actual loading thread and restores the original
instruction before loading HUD, so HUD's code signatures still verify exactly.
Other game threads remain free to load the DLL and initialize its hooks. The
helper resumes table loading only after HUD publishes readiness and the
appropriate comparison is verified. Actual loaded counts of 1–255 rows use
native compatibility mode; 256–512 rows require the extended hooks and a
comparison of 513. The choice uses the game's loaded table, including BIN-only
installs, rather than a possibly absent or outdated states.txt. Distribute the
updated helper with the updated DLL for native compatibility mode. Failure or timeout stops
only the helper's new child. Unsupported code is rejected before execution.
No executable file or mod table is patched on disk.

The gate allows up to two minutes for table loading on slower systems. Timeout,
process exit, invalid table data and rejected HUD initialization return explicit
Windows error codes instead of a stale success code. The progress log prints
the row count and selected compatibility mode; feature-status.txt records the
HUD's state-table mode as well.

Module discovery retries `ERROR_BAD_LENGTH` for up to five seconds. Windows
can invalidate a Toolhelp module snapshot while the HUD's initialization thread
loads another DLL (for example, the diagnostic writer). A transient snapshot
failure must not be interpreted as an injection failure. Other errors return
immediately. See the [Windows Toolhelp documentation](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot).

Older HUD builds retain the existing 1500 ms delayed-injection path. That delay
is too late for an enlarged state table on fast startup: live observation found
259 rows reaching the old assertion before the previous helper loaded HUD.

`MemoryEdits.h` retains the launcher's memory-plan implementation; it is not a
new state patch. Regression tests live in `tests/states-startup-test.cpp` and
`tests/states-512-test.cpp`. Expanded-table activation/expiration, dispelling and
multiplayer still require gameplay checks beyond successful startup.
