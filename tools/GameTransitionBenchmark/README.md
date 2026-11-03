# Passive act/waypoint transition observer

Build and test without attaching to a game:

```powershell
dotnet run --project tools/GameTransitionBenchmark/GameTransitionBenchmark.csproj -c Release -- --self-test true
```

After coordinating the recording with the player, select the exact existing game PID:

```powershell
./tools/benchmark-game-transition.ps1 -GameProcessId 13496 -Seconds 120 -Output "$env:TEMP/act-transitions.json"
```

The default observer requests only process query and memory-read access. It does not launch, terminate, inject, call native functions, alter gameplay, or write game/save/settings files. The player performs transitions normally. Ctrl+C stops observation while leaving the game running.

Native offsets are admitted only after the selected executable's SHA-256 matches the inspected D2R 1.4.71510 build. Other builds are rejected before reading native snapshots.

JSON contains 20 ms target samples, CPU/I/O/working-set/thread metrics at roughly 100 ms, actual live panel changes, location changes, discovered loading-name episodes, and polling overhead. Changes are also flushed live to the adjacent `.events.jsonl` file. `ObserverReady` means sampling has started.

The local player is resolved entirely through existing native data: selector RVA `0x1D442D8`, eight-ID array `0x1BF1E80`, inline player bucket array `0x1D442E0`, bucket `id & 0x7F`, and unit `pListNext +0x150`, matching `ItemFilter.cpp::GetClientPlayerUnit`. Unit act is byte `+0x18` (zero based). Level is unit dynamic path `+0x38` → active room `+0x20` → DRLG room `+0x18` → level `+0x90` → level ID `+0x1F8`, matching `Reclass.h`. All reads are exact and bounded; transient pointer loss is reported.

Visible native widgets are discovered from the existing attached tree rooted at RVA `0x1D7C4E8`, using the same `+0x50/+0x51` flags, `+0x58/+0x60` children, and name pointer `+8` as `QuestJournal::PollMenus`. No creating `FindPanel` lookup is called. Matching `Loading` in live widget names is explicitly a diagnostic heuristic. A level-change timestamp alone does not measure the entire loading-door animation, and an absent HD loading widget does not prove loading is absent.

`Readiness` samples include raw byte RVA `0x1CB410A`, uint64 pending-wait-poll global `0x1DF63C0`, int32 outstanding-work global `0x1CD7020`, and pointer global `0x1C74B48` with the manager's uint64 pending count at `+8`. These are read-only diagnostic fields from the inspected native binary, not a universal readiness guarantee. Changes are also flushed as `ReadinessChanged` events. The now-observed native `LoadScreenPanel` is included explicitly among loading-name candidates.

For graceful early completion, pass a fresh `-StopFile "$env:TEMP/act-observer.stop"` path. Create that file when recording should end. The observer notices it on the next iteration, resumes/finishes profiling, and writes the final JSON without terminating the game. A preexisting stop file is rejected. This avoids losing the in-memory final report when an exec pipeline is forcibly terminated.

Profiling also writes an adjacent `.profile.json` checkpoint atomically at approximately one-second intervals and at shutdown. Serialization happens on the sampler continuation only, after sampled threads have resumed, so mutable dictionaries are never read concurrently. The last complete checkpoint survives an abrupt observer termination. `LoadingInstructionPointerBins` separates loading-name RIP samples into 250 ms bins for comparing the initial asset burst with the later door tail; timestamps use the same observer stopwatch as panel/location events.

For an explicitly authorized diagnostic capture, `-ProfileMilliseconds 40` enables the startup harness's native RIP sampler, pausing/resuming each sampled thread briefly and recording module-relative RIP counts. This mode perturbs timing and is not passive; leave it disabled for baseline transition measurements. Raw stack-address candidates are not unwound call frames.
