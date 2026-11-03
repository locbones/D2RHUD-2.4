using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

var options = args.Chunk(2).ToDictionary(pair => pair[0], pair => pair.Length == 2 ? pair[1] : throw new ArgumentException("Expected --name value pairs."));
if (options.GetValueOrDefault("--self-test") == "true") { GameSnapshotReader.SelfTest(); return 0; }
if (!OperatingSystem.IsWindows() || !Environment.Is64BitProcess) throw new PlatformNotSupportedException("Requires Windows x64.");
var processId = int.Parse(options.GetValueOrDefault("--pid") ?? throw new ArgumentException("An explicit --pid is required."), CultureInfo.InvariantCulture);
var seconds = double.Parse(options.GetValueOrDefault("--seconds", "120"), CultureInfo.InvariantCulture);
var interval = int.Parse(options.GetValueOrDefault("--sample-ms", "20"), CultureInfo.InvariantCulture);
var profileMs = int.Parse(options.GetValueOrDefault("--profile-ms", "0"), CultureInfo.InvariantCulture);
if (seconds is < 1 or > 1800 || interval is < 10 or > 1000 || profileMs != 0 && profileMs is < 20 or > 1000) throw new ArgumentException("Invalid duration or sample interval.");
var output = Path.GetFullPath(options.GetValueOrDefault("--output", Path.Combine(Path.GetTempPath(), "game-transition-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + ".json")));
var eventOutput = Path.ChangeExtension(output, ".events.jsonl");
var profileOutput = Path.ChangeExtension(output, ".profile.json");
var stopFile = options.TryGetValue("--stop-file", out var stopPath) ? Path.GetFullPath(stopPath) : null;
if (stopFile is not null && File.Exists(stopFile)) throw new IOException("Stop file already exists; choose a fresh path or remove the previous signal before recording.");
using var process = Process.GetProcessById(processId);
var processStartUtc = process.StartTime.ToUniversalTime();
// Passive mode requests only query and VM_READ. No game launch, injection, write or process termination API exists in this executable.
var handle = Api.OpenProcess(0x1410, false, (uint)processId);
if (handle == 0) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Could not open selected game for read-only observation.");
using var cancel = new CancellationTokenSource();
Console.CancelKeyPress += (_, eventArgs) => { eventArgs.Cancel = true; cancel.Cancel(); };
var watch = new Stopwatch();
var ticks = new List<Tick>();
var changes = new List<Change>();
var resources = new List<Resource>();
var uiEpisodes = new List<UiEpisode>();
long readCalls = 0, readBytes = 0;
string? failure = null, executable = null;
string? executableHash = null;
DateTime startedUtc = DateTime.UtcNow;
ulong imageBase = 0;
ThreadProfiler? profiler = null;
Task? profiling = null;
string phase = "Unknown";
var profileCancel = new CancellationTokenSource();
Directory.CreateDirectory(Path.GetDirectoryName(output)!);
await using var events = new StreamWriter(eventOutput, false, Encoding.UTF8) { AutoFlush = true };
async Task Report(string kind, object data)
{
    var change = new Change(watch.ElapsedMilliseconds, DateTime.UtcNow, kind, data);
    changes.Add(change);
    var text = JsonSerializer.Serialize(change);
    await events.WriteLineAsync(text);
    Console.WriteLine(text);
}
byte[]? Read(ulong address, int size)
{
    var data = new byte[size];
    readCalls++;
    if (!Api.ReadProcessMemory(handle, (nint)address, data, (nuint)size, out var read) || read != (nuint)size) return null;
    readBytes += (long)read;
    return data;
}
try
{
    var name = new StringBuilder(32768); var length = (uint)name.Capacity;
    if (!Api.QueryFullProcessImageNameW(handle, 0, name, ref length)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    executable = name.ToString();
    if (!Path.GetFileName(executable).Equals("D2R.exe", StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Selected PID is not D2R.exe.");
    var basic = new nint[6];
    if (Api.NtQueryInformationProcess(handle, 0, basic, 48, 0) < 0 || Read((ulong)basic[1] + 0x10, 8) is not { } baseBytes) throw new IOException("Game image base unavailable.");
    imageBase = BitConverter.ToUInt64(baseBytes);
    executableHash = Convert.ToHexString(SHA256.HashData(await File.ReadAllBytesAsync(executable)));
    if (executableHash != "E4E13570E60C0A65CB0469D6659BE8A6008F9433AC50DB1C4F94501D7D9016F8")
        throw new InvalidOperationException("Native transition offsets are verified only for D2R 1.4.71510; this executable differs. No native snapshots were read.");
    var snapshotReader = new GameSnapshotReader(Read, imageBase);
    GameSnapshotReader.Location? lastGoodLocation = null;
    string? lastUiSignature = null;
    GameSnapshotReader.Readiness? lastReadiness = null;
    long resourceAt = -1000;
    Resource? currentResource = null;
    long? episodeStart = null;
    var episodeNames = new HashSet<string>(StringComparer.Ordinal);
    watch.Start();
    if (profileMs != 0)
    {
        // Explicit diagnostic option only: balanced brief thread suspension perturbs timings.
        profiler = new ThreadProfiler(process, () => phase, watch, profileOutput);
        profiling = Task.Run(() => profiler.RunAsync(profileMs, profileCancel.Token));
    }
    await Report("ObserverReady", new { Pid = processId, ImageBase = $"0x{imageBase:X}", ReadOnly = profileMs == 0, IntervalMs = interval, DurationSeconds = seconds, Events = eventOutput, ProfileCheckpoint = profileMs == 0 ? null : profileOutput, StopFile = stopFile });
    while (!cancel.IsCancellationRequested && watch.Elapsed.TotalSeconds < seconds && !process.HasExited && (stopFile is null || !File.Exists(stopFile)))
    {
        var iteration = Stopwatch.StartNew();
        var callsBefore = readCalls;
        var location = snapshotReader.ReadLocation();
        var readiness = snapshotReader.ReadReadiness();
        var uiWatch = Stopwatch.StartNew();
        var ui = snapshotReader.ReadUi();
        uiWatch.Stop();
        phase = ui.LoadingNameCandidates.Length != 0 ? "LoadingNameCandidate" : location.LevelId is { } level ? $"Act{location.ActZeroBased}-Level{level}" : "LocationUnavailable";
        if (watch.ElapsedMilliseconds - resourceAt >= 100)
        {
            process.Refresh();
            Api.GetProcessIoCounters(handle, out var io);
            currentResource = new(watch.ElapsedMilliseconds, process.TotalProcessorTime.TotalMilliseconds, process.WorkingSet64, io.ReadTransferCount, io.WriteTransferCount, io.ReadOperationCount, io.WriteOperationCount, process.Threads.Count);
            resources.Add(currentResource); resourceAt = watch.ElapsedMilliseconds;
        }
        ticks.Add(new(watch.ElapsedMilliseconds, location, readiness, ui.LoadingNameCandidates, ui.Error, ui.VisitedNodes, uiWatch.Elapsed.TotalMilliseconds, readCalls - callsBefore));
        if (readiness != lastReadiness) { await Report("ReadinessChanged", readiness); lastReadiness = readiness; }
        if (location.LevelId is not null && location.ActZeroBased is not null)
        {
            if (lastGoodLocation is null || location.LevelId != lastGoodLocation.LevelId || location.ActZeroBased != lastGoodLocation.ActZeroBased)
                await Report("LocationChanged", new { Previous = lastGoodLocation, Current = location });
            lastGoodLocation = location;
        }
        var uiSignature = string.Join('|', ui.Widgets.Select(widget => widget.Depth + ":" + widget.Name)) + ";" + ui.Error;
        if (uiSignature != lastUiSignature)
        {
            await Report("VisiblePanelsChanged", ui);
            lastUiSignature = uiSignature;
        }
        // Names are discovered from the live attached tree. A name match is a diagnostic heuristic,
        // not a claim that every loading door is represented by an HD widget.
        if (ui.Error is null)
        {
            if (ui.LoadingNameCandidates.Length != 0)
            {
                episodeStart ??= watch.ElapsedMilliseconds;
                foreach (var candidate in ui.LoadingNameCandidates) episodeNames.Add(candidate);
            }
            else if (episodeStart is { } start)
            {
                var episode = new UiEpisode(start, watch.ElapsedMilliseconds, watch.ElapsedMilliseconds - start, episodeNames.Order().ToArray(), false);
                uiEpisodes.Add(episode); await Report("LoadingNameEpisodeEnded", episode);
                episodeStart = null; episodeNames.Clear();
            }
        }
        var remaining = interval - iteration.Elapsed.TotalMilliseconds;
        if (remaining > 0) try { await Task.Delay(TimeSpan.FromMilliseconds(remaining), cancel.Token); } catch (OperationCanceledException) { break; }
    }
    if (episodeStart is { } unfinished) uiEpisodes.Add(new(unfinished, watch.ElapsedMilliseconds, watch.ElapsedMilliseconds - unfinished, episodeNames.Order().ToArray(), true));
}
catch (Exception error) { failure = error.ToString(); }
finally
{
    profileCancel.Cancel();
    if (profiling is not null) await profiling;
    watch.Stop();
    var report = new {
        SchemaVersion = 1, StartedUtc = startedUtc, Pid = processId, ProcessStartUtc = processStartUtc, Executable = executable, ExecutableSha256 = executableHash, ImageBase = $"0x{imageBase:X}",
        Mode = profileMs == 0 ? "Passive query/ReadProcessMemory only" : "Explicit diagnostic RIP sampling with balanced thread suspension",
        RequestedIntervalMs = interval, RequestedSeconds = seconds, ObservedMs = watch.ElapsedMilliseconds, ReadCalls = readCalls, ReadBytes = readBytes, StopFile = stopFile, StopFileObserved = stopFile is not null && File.Exists(stopFile), ProfileCheckpoint = profileMs == 0 ? null : profileOutput,
        Definitions = new { Act = "Native zero-based unit act byte; UI act number is this value plus one.", Location = "Client player selected by native ID array and hash bucket, followed through dynamic path/room/DRLG room/level. LocationChanged marks observation of a new native level, not the complete door animation.", LoadingNameEpisode = "Attached visible native widget names containing Loading, discovered rather than assumed. This is a diagnostic heuristic; absent widgets do not prove absent loading.", VisiblePanels = "Read-only attached enabled/visible widget tree with enabled/visible ancestors. No native UI lookup/create calls or gameplay inputs.", Timings = "Passive polling has sampling granularity and overhead recorded per tick; optional RIP profiling perturbs timings." },
        Failure = failure, Changes = changes, Samples = ticks, Resources = resources, LoadingNameEpisodes = uiEpisodes, Profiling = profiler?.Report()
    };
    await File.WriteAllTextAsync(output, JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
    Api.CloseHandle(handle);
    profileCancel.Dispose();
    Console.WriteLine("RESULT " + output);
}
return failure is null ? 0 : 1;

sealed record Tick(long ElapsedMs, GameSnapshotReader.Location Location, GameSnapshotReader.Readiness Readiness, string[] LoadingNameCandidates, string? UiError, int UiNodes, double UiReadMs, long ReadCalls);
sealed record Resource(long ElapsedMs, double CpuMs, long WorkingSetBytes, ulong ReadBytes, ulong WriteBytes, ulong ReadOperations, ulong WriteOperations, int Threads);
sealed record Change(long ElapsedMs, DateTime TimestampUtc, string Kind, object Data);
sealed record UiEpisode(long StartMs, long EndMs, long ObservedDurationMs, string[] Names, bool Incomplete);
static class Api
{
    [StructLayout(LayoutKind.Sequential)] internal struct IoCounters { internal ulong ReadOperationCount, WriteOperationCount, OtherOperationCount, ReadTransferCount, WriteTransferCount, OtherTransferCount; }
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern nint OpenProcess(uint access, [MarshalAs(UnmanagedType.Bool)] bool inherit, uint id);
    [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool ReadProcessMemory(nint process, nint address, byte[] data, nuint length, out nuint read);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool QueryFullProcessImageNameW(nint process, uint flags, StringBuilder name, ref uint size);
    [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool GetProcessIoCounters(nint process, out IoCounters counters);
    [DllImport("ntdll.dll")] internal static extern int NtQueryInformationProcess(nint process, int infoClass, nint[] info, int length, nint returned);
    [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool CloseHandle(nint handle);
}
