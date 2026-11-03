using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.InteropServices;

// Optional diagnostic mode only. Balanced thread pauses perturb runtime and must not be used
// for timing A/B measurements. The sampler touches only threads in its retained target process.
sealed class ThreadProfiler(Process process, Func<string> phase, Stopwatch watch, string? checkpointPath = null)
{
    readonly Dictionary<(string Phase, int Thread, ulong Rip), int> samples = new();
    readonly Dictionary<(string Phase, ulong Address), int> stackWords = new();
    readonly Dictionary<(int Bin, int Thread, ulong Rip), int> loadingTimeBins = new();
    readonly List<Module> modules = new();
    readonly List<string> errors = new();
    long samplePasses, sampled, alreadyPaused, exited, contextFailures, suspendFailures, resumeFailures;
    double samplingMs;
    const int loadingBinMs = 250;
    long lastCheckpointMs = -1000;
    void Checkpoint(bool force = false)
    {
        // Run only on the sampler continuation, after every sampled thread was resumed.
        // Never enumerate mutable dictionaries concurrently from the observer thread.
        if (checkpointPath is null || !force && watch.ElapsedMilliseconds - lastCheckpointMs < 1000) return;
        try
        {
            var path = Path.GetFullPath(checkpointPath);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var temporary = path + ".tmp";
            File.WriteAllText(temporary, System.Text.Json.JsonSerializer.Serialize(Report()));
            File.Move(temporary, path, overwrite: true);
            lastCheckpointMs = watch.ElapsedMilliseconds;
        }
        catch (Exception error) { if (errors.Count < 20) errors.Add("Checkpoint: " + error.Message); }
    }
    public async Task RunAsync(int intervalMs, CancellationToken cancellationToken)
    {
        var raw = Marshal.AllocHGlobal(1248);
        var context = (nint)(((long)raw + 15) & ~15L);
        var stack = new byte[512];
        long moduleRefreshAt = -1000;
        try
        {
            while (!cancellationToken.IsCancellationRequested && !process.HasExited)
            {
                var pass = Stopwatch.StartNew();
                samplePasses++;
                if (watch.ElapsedMilliseconds - moduleRefreshAt >= 250)
                {
                    try
                    {
                        foreach (ProcessModule module in process.Modules)
                            if (!modules.Any(m => m.Base == (ulong)module.BaseAddress))
                                modules.Add(new(module.ModuleName, module.FileName, (ulong)module.BaseAddress, (ulong)module.ModuleMemorySize));
                    }
                    catch (Exception error) { if (errors.Count < 20) errors.Add("Module enumeration: " + error.Message); }
                    moduleRefreshAt = watch.ElapsedMilliseconds;
                }
                try
                {
                    process.Refresh();
                    foreach (ProcessThread thread in process.Threads)
                    {
                        using (thread)
                        {
                            if (cancellationToken.IsCancellationRequested) break;
                            var handle = Api.OpenThread(0x10004a, false, (uint)thread.Id);
                            if (handle == 0) { exited++; continue; }
                            var suspended = false;
                            try
                            {
                                if (Api.GetProcessIdOfThread(handle) != process.Id || Api.WaitForSingleObject(handle, 0) != 258) { exited++; continue; }
                                var prior = Api.SuspendThread(handle);
                                if (prior == uint.MaxValue) { suspendFailures++; continue; }
                                suspended = true;
                                if (prior != 0) { alreadyPaused++; continue; }
                                Marshal.WriteInt32(context, 48, 0x100001); // AMD64 CONTEXT_CONTROL: RIP/RSP.
                                if (!Api.GetThreadContext(handle, context)) { contextFailures++; continue; }
                                var rip = (ulong)Marshal.ReadInt64(context, 248);
                                var rsp = (nint)Marshal.ReadInt64(context, 152);
                                var currentPhase = phase();
                                var key = (currentPhase, thread.Id, rip);
                                samples[key] = samples.GetValueOrDefault(key) + 1;
                                if (currentPhase == "LoadingNameCandidate")
                                {
                                    var timedKey = ((int)(watch.ElapsedMilliseconds / loadingBinMs), thread.Id, rip);
                                    loadingTimeBins[timedKey] = loadingTimeBins.GetValueOrDefault(timedKey) + 1;
                                }
                                sampled++;
                                // Unwind-free stack words are candidates, not proven call frames.
                                // Retain only EXE/HUD addresses for identifying callers waiting inside Windows.
                                if (Api.ReadProcessMemory(process.Handle, rsp, stack, (nuint)stack.Length, out var read))
                                    for (var i = 0; i + 8 <= (long)read; i += 8)
                                    {
                                        var candidate = BitConverter.ToUInt64(stack, i);
                                        if (!modules.Any(m => (m.Name.Equals("D2R.exe", StringComparison.OrdinalIgnoreCase) || m.Name.Equals("d2rhud.dll", StringComparison.OrdinalIgnoreCase)) && candidate >= m.Base && candidate - m.Base < m.Size)) continue;
                                        var stackKey = (currentPhase, candidate);
                                        stackWords[stackKey] = stackWords.GetValueOrDefault(stackKey) + 1;
                                    }
                            }
                            finally
                            {
                                if (suspended && Api.ResumeThread(handle) == uint.MaxValue) resumeFailures++;
                                Api.CloseHandle(handle);
                            }
                        }
                    }
                }
                catch (Exception error) { if (errors.Count < 20) errors.Add("Sampling: " + error.Message); }
                samplingMs += pass.Elapsed.TotalMilliseconds;
                Checkpoint();
                try { await Task.Delay(intervalMs, cancellationToken); } catch (OperationCanceledException) { break; }
            }
        }
        finally { Checkpoint(force: true); Marshal.FreeHGlobal(raw); }
    }
    object Location(ulong address)
    {
        var module = modules.FirstOrDefault(m => address >= m.Base && address - m.Base < m.Size);
        return new { Address = $"0x{address:X}", Module = module?.Name, Offset = module is null ? null : $"0x{address - module.Base:X}" };
    }
    public object Report() => new {
        Description = "AMD64 native RIP samples from every retained target-process thread, with balanced SuspendThread/GetThreadContext/ResumeThread; never enable for baseline timing.",
        StackWordCaveat = "Stack words are raw EXE/HUD address candidates, not an unwound call stack or proven return addresses.",
        ProcessId = process.Id, ElapsedMs = watch.ElapsedMilliseconds, CheckpointUtc = DateTime.UtcNow, LoadingBinWidthMs = loadingBinMs,
        SamplePasses = samplePasses, Samples = sampled, AlreadyPausedSkipped = alreadyPaused, ExitedSkipped = exited,
        ContextFailures = contextFailures, SuspendFailures = suspendFailures, ResumeFailures = resumeFailures, SamplingWorkMs = samplingMs,
        Modules = modules, Errors = errors,
        InstructionPointers = samples.OrderByDescending(x => x.Value).Select(x => new { x.Key.Phase, ThreadId = x.Key.Thread, Location = Location(x.Key.Rip), Count = x.Value }).ToArray(),
        StackAddressCandidates = stackWords.OrderByDescending(x => x.Value).Take(2000).Select(x => new { x.Key.Phase, Location = Location(x.Key.Address), Count = x.Value }).ToArray(),
        LoadingInstructionPointerBins = loadingTimeBins.OrderBy(x => x.Key.Bin).ThenByDescending(x => x.Value).Select(x => new { StartMs = (long)x.Key.Bin * loadingBinMs, EndMs = ((long)x.Key.Bin + 1) * loadingBinMs, ThreadId = x.Key.Thread, Location = Location(x.Key.Rip), Count = x.Value }).ToArray()
    };
    sealed record Module(string Name, string Path, ulong Base, ulong Size);
    static class Api
    {
        [DllImport("kernel32.dll", SetLastError = true)] internal static extern nint OpenThread(uint access, [MarshalAs(UnmanagedType.Bool)] bool inherit, uint id);
        [DllImport("kernel32.dll")] internal static extern uint GetProcessIdOfThread(nint thread);
        [DllImport("kernel32.dll")] internal static extern uint WaitForSingleObject(nint handle, uint ms);
        [DllImport("kernel32.dll")] internal static extern uint SuspendThread(nint thread);
        [DllImport("kernel32.dll")] internal static extern uint ResumeThread(nint thread);
        [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool GetThreadContext(nint thread, nint context);
        [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool ReadProcessMemory(nint process, nint address, byte[] data, nuint length, out nuint read);
        [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool CloseHandle(nint handle);
    }
}
