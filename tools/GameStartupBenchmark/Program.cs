using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
using Pandemonium.Core;

if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("This benchmark uses the native Windows launcher.");
var options = args.Chunk(2).ToDictionary(p => p[0], p => p.Length == 2 ? p[1] : throw new ArgumentException("Arguments are --name value pairs."));
if (options.GetValueOrDefault("--self-test") == "true") { NativeFrontendObserver.SelfTest(); LabLaunchGuard.SelfTest(); return 0; }
var label = options.GetValueOrDefault("--label", "baseline");
var output = Path.GetFullPath(options.GetValueOrDefault("--output", Path.Combine(Path.GetTempPath(), "game-startup-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + ".json")));
var observeSeconds = double.Parse(options.GetValueOrDefault("--observe-seconds", "8"), CultureInfo.InvariantCulture);
var timeoutSeconds = double.Parse(options.GetValueOrDefault("--timeout-seconds", "45"), CultureInfo.InvariantCulture);
var sampleInterval = int.Parse(options.GetValueOrDefault("--sample-ms", "100"), CultureInfo.InvariantCulture);
var profileInterval = int.Parse(options.GetValueOrDefault("--profile-ms", "0"), CultureInfo.InvariantCulture);
var waitForFrontend = bool.Parse(options.GetValueOrDefault("--frontend", "true"));
var keepOpenLab = bool.Parse(options.GetValueOrDefault("--keep-open-lab", "false"));
if (observeSeconds is < 0 or > 120 || timeoutSeconds is < 5 or > 180 || sampleInterval is < 10 or > 1000) throw new ArgumentException("Invalid observation/timeout/sample interval.");
if (profileInterval != 0 && profileInterval is < 20 or > 1000) throw new ArgumentException("Profile interval must be zero (off) or 20..1000 ms.");
using (var existing = Process.GetProcessesByName("D2R").FirstOrDefault())
    if (existing is not null) throw new InvalidOperationException($"D2R PID {existing.Id} is already running; no game was launched or terminated.");
var store = options.TryGetValue("--settings", out var settingsPath) ? new SettingsStore(settingsPath) : SettingsStore.ForCurrentUser();
var configuration = await store.LoadAsync();
var settings = configuration.Version24;
const string modId = "RMD-MP";
if (settings.ModProfiles.TryGetValue(modId, out var modProfile)) settings = modProfile.ApplyTo(settings);
settings = settings.WithResolvedRuntime();
if (keepOpenLab)
{
    if (!waitForFrontend) throw new InvalidOperationException("Keep-open lab mode requires native frontend confirmation.");
    LabLaunchGuard.Require(settings.GameDirectory, Environment.GetEnvironmentVariable("LOCALAPPDATA"), Path.GetTempPath());
    LabLaunchGuard.RequirePreparedFiles(settings.GameDirectory);
}
if (bool.Parse(options.GetValueOrDefault("--validate-only", "false"))) { Console.WriteLine("VALIDATED without launching: " + settings.GameDirectory); return 0; }
if (options.TryGetValue("--compile-text", out var compileText)) settings = settings with { CompileText = bool.Parse(compileText) };
var mod = new InstalledMod(modId, modId, Path.Combine(settings.GameDirectory, "mods", modId, modId + ".mpq"), "benchmark");
var start = new GameLauncher().CreateStartInfo(settings, mod, isWindows: true, GameMode.Version24);
if (bool.Parse(options.GetValueOrDefault("--disable-profiler", "false"))) start.ArgumentList.Add("-noprofiler");
var executable = Path.GetFullPath(start.FileName);
var hud = Path.Combine(start.WorkingDirectory, "d2rhud.dll");
var stages = new ConcurrentQueue<Stage>();
var samples = new List<Sample>();
var frontendObservations = new List<FrontendObservation>();
var watch = new Stopwatch();
var startedUtc = DateTime.UtcNow;
Process? owned = null;
var ownedLock = new object();
long? firstVisible = null, firstInputIdle = null, completion = null;
long? nativeFrontendVisible = null, nativeFrontendConfirmed = null;
string? nativeFrontendPanel = null;
NativeFrontendObserver.Snapshot? lastFrontendSnapshot = null;
ulong imageBase = 0;
ulong? tracySingletonAtProtocol = null, tracySingletonAtFrontend = null;
int? threadsAtFrontend = null;
bool leftRunning = false;
bool labWindowTitleApplied = false;
const string labWindowTitle = "Act loading test — copied saves";
var frontendConsecutive = 0;
long completionMarker = -1;
string? failure = null;
int? gamePid = null;
DateTime? gameStart = null;
using var profileCancel = new CancellationTokenSource();
Task? profileTask = null;
ThreadProfiler? profiler = null;
string phase = "BeforeStateTableGate";
var settingsHashBefore = Hash(store.FilePath);
var progress = new InlineProgress(message => {
    var stage = new Stage(watch.ElapsedMilliseconds, DateTime.UtcNow, message);
    stages.Enqueue(stage);
    if (message.StartsWith("Loading D2RHUD", StringComparison.Ordinal)) phase = "HudInitialization";
    if (message.StartsWith("HUD hooks and state-table", StringComparison.Ordinal)) phase = "AfterHudReady";
    if (!message.StartsWith("Memory edit ", StringComparison.Ordinal))
        Console.WriteLine($"{stage.ElapsedMs,6} ms | {message}");
    var match = Regex.Match(message, @"^Created game process (\d+)");
    if (match.Success)
    {
        var child = Process.GetProcessById(int.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture));
        var childPath = new System.Text.StringBuilder(32768);
        var pathLength = (uint)childPath.Capacity;
        if (!Native.QueryFullProcessImageNameW(child.Handle, 0, childPath, ref pathLength) ||
            !string.Equals(Path.GetFullPath(childPath.ToString()), executable, StringComparison.OrdinalIgnoreCase))
        {
            child.Dispose();
            throw new InvalidOperationException("Created process executable did not match the requested game; refusing ownership.");
        }
        lock (ownedLock) { owned = child; gamePid = child.Id; gameStart = child.StartTime.ToUniversalTime(); }
        if (profileInterval != 0)
        {
            profiler = new ThreadProfiler(child, () => phase, watch);
            profileTask = Task.Run(() => profiler.RunAsync(profileInterval, profileCancel.Token));
        }
    }
});
var executableHash = Hash(executable);
var hudHash = Hash(hud);
var hostProcesses = Process.GetProcesses().Select(p => { using (p) { try { return new { p.Id, p.ProcessName, CpuMs = (double?)p.TotalProcessorTime.TotalMilliseconds }; } catch { return new { p.Id, p.ProcessName, CpuMs = (double?)null }; } } }).ToArray();
var preparation = Stopwatch.StartNew();
var plan = await new MemoryEditService().PrepareAsync(settings, mod, new InlineProgress(Console.WriteLine));
preparation.Stop();
Task? launch = null;
try
{
    // This calls production suspension/gating, DLL loading, readiness checks and memory edits.
    // No updater, download, frontend input, character selection or save writing is performed here.
    watch.Start();
    async Task TrackLaunch()
    {
        await WindowsHudLauncher.LaunchAsync(start, plan, progress);
        Interlocked.Exchange(ref completionMarker, watch.ElapsedMilliseconds);
    }
    launch = TrackLaunch();
    while (true)
    {
        if (launch.IsCompleted && completion is null) { await launch; completion = Interlocked.Read(ref completionMarker); }
        Process? child;
        lock (ownedLock) child = owned;
        if (child is not null)
        {
            child.Refresh();
            if (child.HasExited) { failure = $"Game exited with code {child.ExitCode} before observation completed."; break; }
            var window = child.MainWindowHandle;
            if (window != 0 && Native.IsWindowVisible(window)) firstVisible ??= watch.ElapsedMilliseconds;
            try { if (firstInputIdle is null && child.WaitForInputIdle(0)) firstInputIdle = watch.ElapsedMilliseconds; } catch (InvalidOperationException) { }
            Native.GetProcessIoCounters(child.Handle, out var io);
            samples.Add(new(watch.ElapsedMilliseconds, child.TotalProcessorTime.TotalMilliseconds, child.WorkingSet64,
                io.ReadOperationCount, io.ReadTransferCount, io.WriteOperationCount, io.WriteTransferCount, child.Threads.Count, window != 0 && Native.IsWindowVisible(window)));
            if (waitForFrontend && completion is not null && nativeFrontendConfirmed is null)
            {
                byte[]? Read(ulong address, int length)
                {
                    var bytes = new byte[length];
                    return Native.ReadProcessMemory(child.Handle, (nint)address, bytes, (nuint)length, out var read) && read == (nuint)length ? bytes : null;
                }
                if (imageBase == 0)
                {
                    var basic = new nint[6];
                    if (Native.NtQueryInformationProcess(child.Handle, 0, basic, 48, 0) >= 0 &&
                        Read((ulong)basic[1] + 0x10, 8) is { } imageBytes) imageBase = BitConverter.ToUInt64(imageBytes);
                }
                if (tracySingletonAtProtocol is null && imageBase != 0 && Read(imageBase + 0x1dc6d80, 8) is { } tracyBytes)
                    tracySingletonAtProtocol = BitConverter.ToUInt64(tracyBytes);
                var snapshot = NativeFrontendObserver.Observe(Read, imageBase);
                if (snapshot != lastFrontendSnapshot)
                {
                    frontendObservations.Add(new(watch.ElapsedMilliseconds, snapshot));
                    lastFrontendSnapshot = snapshot;
                }
                if (snapshot.Ready)
                {
                    if (nativeFrontendPanel != snapshot.PanelName) { frontendConsecutive = 0; nativeFrontendVisible = null; }
                    nativeFrontendPanel = snapshot.PanelName;
                    nativeFrontendVisible ??= watch.ElapsedMilliseconds;
                    if (++frontendConsecutive >= 3)
                    {
                        nativeFrontendConfirmed = watch.ElapsedMilliseconds;
                        threadsAtFrontend = child.Threads.Count;
                        if (Read(imageBase + 0x1dc6d80, 8) is { } frontendTracyBytes) tracySingletonAtFrontend = BitConverter.ToUInt64(frontendTracyBytes);
                        Console.WriteLine($"{nativeFrontendConfirmed,6} ms | NativeFrontendVisible confirmed: {nativeFrontendPanel}; no visible MainMenuLoadingPanel.");
                    }
                }
                else { frontendConsecutive = 0; nativeFrontendVisible = null; nativeFrontendPanel = null; }
            }
        }
        var observationStart = waitForFrontend ? nativeFrontendConfirmed : completion;
        if (observationStart is { } ready && watch.Elapsed.TotalMilliseconds >= ready + observeSeconds * 1000)
        {
            if (keepOpenLab)
            {
                lock (ownedLock) child = owned;
                child?.Refresh();
                var window = child?.MainWindowHandle ?? 0;
                labWindowTitleApplied = window != 0 && Native.SetWindowTextW(window, labWindowTitle);
                if (!labWindowTitleApplied) throw new IOException("Could not label the isolated lab game window; refusing to leave it running.");
            }
            break;
        }
        if (observationStart is null && watch.Elapsed.TotalSeconds >= timeoutSeconds)
        {
            failure = completion is null ? "Production launch exceeded benchmark timeout." : "NativeFrontendVisible was not confirmed before benchmark timeout; HUD readiness is not frontend readiness.";
            break;
        }
        await Task.Delay(sampleInterval);
    }
}
catch (Exception error) { failure = error.ToString(); }
finally
{
    profileCancel.Cancel();
    if (profileTask is not null) await profileTask;
    Process? child;
    lock (ownedLock) child = owned;
    if (child is not null)
    {
        try
        {
            // The retained Process handle identifies only our reported child, even if Windows reuses a PID.
            if (!child.HasExited)
            {
                leftRunning = keepOpenLab && failure is null && completion is not null && nativeFrontendConfirmed is not null && labWindowTitleApplied;
                if (!leftRunning) { child.Kill(entireProcessTree: false); await child.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10)); }
            }
        }
        catch (Exception error) { failure = (failure is null ? "" : failure + "\n") + "Owned-child cleanup failed: " + error.Message; }
        child.Dispose();
    }
    if (launch is not null)
    {
        try { await launch.WaitAsync(TimeSpan.FromSeconds(10)); }
        catch (Exception error) { failure ??= error.ToString(); }
    }
    watch.Stop();
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    var record = new {
        SchemaVersion = 1, Label = label, StartedUtc = startedUtc, StopwatchFrequency = Stopwatch.Frequency,
        Environment = new { Machine = Environment.MachineName, ProcessorCount = Environment.ProcessorCount, OS = Environment.OSVersion.ToString(), Is64BitProcess = Environment.Is64BitProcess },
        Settings = new { Path = store.FilePath, BeforeSha256 = settingsHashBefore, AfterSha256 = Hash(store.FilePath), PersistedSettingsWereWritten = false, settings.CompileText, WindowMode = settings.WindowMode.ToString(), settings.SkipLogos, settings.NoSound, settings.ForceLowend },
        Executable = new { Path = executable, Sha256 = executableHash }, Hud = new { Path = hud, Sha256 = hudHash },
        Arguments = start.ArgumentList.ToArray(), start.WorkingDirectory, ModDirectory = mod.Directory,
        MemoryPlan = new { PreparationMs = preparation.Elapsed.TotalMilliseconds, Patches = plan.Patches.Count, plan.Skipped },
        Game = new { Pid = gamePid, StartedUtc = gameStart, LeftRunning = leftRunning, KeepOpenLab = keepOpenLab, WindowTitle = labWindowTitleApplied ? labWindowTitle : null },
        Metrics = new { ProductionLaunchCompletedMs = completion, FirstVisibleWindowMs = firstVisible, FirstInputIdleMs = firstInputIdle, NativeFrontendVisibleMs = nativeFrontendVisible, NativeFrontendConfirmedMs = nativeFrontendConfirmed, NativeFrontendPanelName = nativeFrontendPanel, TotalObservedMs = watch.ElapsedMilliseconds },
        NativeProfilerProbe = new { GlobalRva = "0x1DC6D80", SingletonAtProtocol = tracySingletonAtProtocol is { } protocolPointer ? $"0x{protocolPointer:X}" : null, SingletonAtFrontend = tracySingletonAtFrontend is { } frontendPointer ? $"0x{frontendPointer:X}" : null, ThreadsAtFrontend = threadsAtFrontend, Meaning = "Read-only native Tracy singleton pointer and process thread count; diagnostic observations do not alone prove profiler activity or inactivity." },
        Definitions = new { ProductionLaunchCompleted = "Native HUD gating/loading/ready protocol and configured memory edits completed; not menu/render readiness.", NativeFrontendVisible = "Read-only attached native MainMenuPanel or CharacterSelectPanel visible through all live ancestors, with no visible MainMenuLoadingPanel; confirmed across three samples. This does not prove GPU frame presentation.", FirstVisibleWindow = "First visible top-level window; not menu or character-screen readiness.", FirstInputIdle = "Win32 input-idle signal; not rendering readiness.", Observation = "No frontend input or character load. Only this invocation's child can be stopped; successful guarded keep-open lab mode instead returns that child PID." },
        Failure = failure, Stages = stages.ToArray(), Samples = samples, NativeFrontendObservations = frontendObservations, HostProcessesAtStart = hostProcesses
        , Profiling = profiler?.Report()
    };
    await File.WriteAllTextAsync(output, JsonSerializer.Serialize(record, new JsonSerializerOptions { WriteIndented = true }));
    Console.WriteLine($"RESULT {output}");
    if (leftRunning) Console.WriteLine($"LAB_GAME_LEFT_RUNNING PID {gamePid}: {labWindowTitle}");
}
return failure is null ? 0 : 1;

static string? Hash(string path) => File.Exists(path) ? Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))) : null;
sealed class InlineProgress(Action<string> action) : IProgress<string> { public void Report(string value) => action(value); }
sealed record Stage(long ElapsedMs, DateTime TimestampUtc, string Message);
sealed record Sample(long ElapsedMs, double CpuMs, long WorkingSetBytes, ulong ReadOperations, ulong ReadBytes, ulong WriteOperations, ulong WriteBytes, int Threads, bool VisibleWindow);
sealed record FrontendObservation(long ElapsedMs, NativeFrontendObserver.Snapshot Snapshot);
static class Native
{
    [StructLayout(LayoutKind.Sequential)] internal struct IoCounters { internal ulong ReadOperationCount, WriteOperationCount, OtherOperationCount, ReadTransferCount, WriteTransferCount, OtherTransferCount; }
    [DllImport("kernel32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool GetProcessIoCounters(nint process, out IoCounters counters);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool QueryFullProcessImageNameW(nint process, uint flags, System.Text.StringBuilder name, ref uint size);
    [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool ReadProcessMemory(nint process, nint address, byte[] data, nuint length, out nuint read);
    [DllImport("ntdll.dll")] internal static extern int NtQueryInformationProcess(nint process, int informationClass, nint[] information, int length, nint returnedLength);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool IsWindowVisible(nint window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool SetWindowTextW(nint window, string text);
}
