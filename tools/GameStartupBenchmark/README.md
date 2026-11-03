# Native game startup benchmark

This Windows x64 .NET 10 harness uses the production `WindowsHudLauncher`, loading the current user's `SettingsStore.Version24` and applying the `RMD-MP` mod profile. It prepares the real memory-edit plan and uses `GameLauncher.CreateStartInfo`. It bypasses downloads, update checks, launcher UI, and prelaunch QoL preparation, so it measures game/HUD startup rather than total launcher workflow.

Run from the repository root:

```powershell
./tools/benchmark-game-startup.ps1 -Label baseline -Output "$env:TEMP/startup-baseline.json"
```

The project references the sibling Pandemonium Core repository using `CoreProject` in its project file. Change that property for another checkout. The benchmark assembly uses the existing test friend-assembly name to access the production internal launcher without changing production source.

The harness refuses to start if any `D2R` process is already running. It adopts only the PID reported by its own launch and verifies the executable path. It sends no frontend input and never selects a character. By default, it waits for `NativeFrontendVisible` and then observes eight more seconds before stopping only that retained child process. Timeout and error paths use the same ownership rule. Normal game startup may generate runtime caches and logs; text compilation follows the current launcher setting by default.

JSON records executable and HUD SHA-256 hashes, exact arguments, progress stages with elapsed times, CPU time, working set, process I/O, thread counts, and host-process context. The settings file is never saved by the harness; its before/after hashes are recorded. `MemoryEditService.PrepareAsync` retains its normal configuration-merge behavior.

`ProductionLaunchCompletedMs` verifies the HUD startup protocol and memory patches. A visible window or Win32 input-idle signal does **not** establish that the main menu or character screen has rendered. Compare identical endpoints, builds, settings, and warm/cold-cache conditions across repeated interleaved runs.

`NativeFrontendVisibleMs` is the beginning of three consecutive read-only samples showing a native attached `MainMenuPanel` or `CharacterSelectPanel` with enabled/visible live ancestors and no visible `MainMenuLoadingPanel`. `NativeFrontendConfirmedMs` is the third sample. This establishes the native UI state; it does not prove GPU frame presentation. Traversal uses the bounded, noncreating widget-tree layout from `QuestJournal::PollMenus`; it never calls `FindPanel`, which can create absent panels. If the endpoint is not observed within the timeout, it reports failure without substituting HUD readiness. To observe only the earlier protocol endpoint, explicitly pass `-Frontend $false`.

The observer's synthetic memory-reader tests run without launching a game:

```powershell
dotnet run --project tools/GameStartupBenchmark/GameStartupBenchmark.csproj -c Release -- --self-test true
```

Optional diagnostic mode:

```powershell
./tools/benchmark-game-startup.ps1 -Label profile -ProfileMilliseconds 40 -Output "$env:TEMP/startup-profile.json"
```

Profiling samples native AMD64 instruction pointers from all threads of the owned game. Every successful suspension is balanced with a resume in `finally`; threads already suspended are skipped. This mode perturbs runtime and must not be used for timing A/B comparisons. Module-relative RIP counts are genuine observations; raw stack-address candidates are not an unwound call stack.

Use `-CompileText On` or `-CompileText Off` only for deliberate session-only comparisons; this does not change persisted launcher settings. `-Settings`, `-ObserveSeconds`, and `-TimeoutSeconds` override the corresponding harness inputs.

`-DisableProfiler` appends the game's native `-noprofiler` command-line option for that one run, without changing persisted settings. The full effective argument list is recorded. `NativeProfilerProbe` records the read-only Tracy singleton global at RVA `0x1DC6D80` after the HUD protocol and at native frontend confirmation, plus the frontend process thread count. These diagnostic values alone do not establish whether the profiler is active; compare against the native binary and timing samples.

`-KeepOpenLab` is reserved for an isolated copy prepared by `tools/prepare-act-load-lab.ps1`. The game directory must be a direct `TEMP/D2RHUD-act-load-lab-<id>` child, not a root junction, and the **launching process's** `LOCALAPPDATA` must equal that lab's private `LocalAppData` directory. Set the environment before starting the harness and pass the private `-Settings` file. After successful HUD and native frontend confirmation, the harness labels the window **Act loading test — copied saves**, returns the owned PID, and leaves that lab game running for manual interaction. Failures still stop only its own child. Ordinary benchmark mode continues stopping its own child normally. Native frontend confirmation cannot be disabled in keep-open mode.
