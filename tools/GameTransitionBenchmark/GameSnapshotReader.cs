using System.Text;

sealed class GameSnapshotReader(Func<ulong, int, byte[]?> reader, ulong imageBase)
{
    internal sealed record Location(byte? ClientState, string? LocalServerClientHeadPointer, uint? Selector, uint? PlayerId, string? PlayerPointer, byte? ActZeroBased, uint? LevelId, string? Error);
    internal sealed record VisibleWidget(string Name, int Depth);
    internal sealed record Ui(VisibleWidget[] Widgets, string[] LoadingNameCandidates, int VisitedNodes, string? Error);
    internal sealed record Readiness(byte? NativeReadyByte1CB410A, ulong? PendingWaitPolls1DF63C0, int? OutstandingWork1CD7020, string? QueueManager1C74B48, ulong? QueueManagerPendingAt8);
    static bool Pointer(ulong pointer) => pointer >= 0x10000 && pointer < 0x800000000000;
    byte[]? Read(ulong address, int size)
    {
        if (!Pointer(address) || size is < 1 or > 8192 || address + (ulong)size < address) return null;
        var bytes = reader(address, size);
        return bytes?.Length == size ? bytes : null;
    }
    internal Location ReadLocation()
    {
        byte? state = Read(imageBase + 0x1dc76f8, 1)?[0];
        var localServerBytes = Read(imageBase + 0x1d637f0, 8);
        var localServerPointer = localServerBytes is null ? null : $"0x{BitConverter.ToUInt64(localServerBytes):X}";
        uint? selector = null, id = null;
        string? pointer = null;
        Location Failed(string reason) => new(state, localServerPointer, selector, id, pointer, null, null, reason);
        var selectorBytes = Read(imageBase + 0x1d442d8, 4);
        if (selectorBytes is null) return Failed("Player selector unreadable.");
        selector = BitConverter.ToUInt32(selectorBytes);
        if (selector >= 8) return Failed("No selected local player.");
        var idBytes = Read(imageBase + 0x1bf1e80 + selector.Value * 4, 4);
        if (idBytes is null) return Failed("Local player ID unreadable.");
        id = BitConverter.ToUInt32(idBytes);
        var bucket = Read(imageBase + 0x1d442e0 + (id.Value & 0x7f) * 8, 8);
        if (bucket is null) return Failed("Player hash bucket unreadable.");
        var unit = BitConverter.ToUInt64(bucket);
        var seen = new HashSet<ulong>();
        while (unit != 0 && seen.Count < 128 && seen.Add(unit))
        {
            var header = Read(unit, 0x158);
            if (header is null) return Failed("Player hash entry unreadable.");
            if (BitConverter.ToUInt32(header, 0) == 0 && BitConverter.ToUInt32(header, 8) == id)
            {
                pointer = $"0x{unit:X}";
                var act = header[0x18];
                var path = BitConverter.ToUInt64(header, 0x38);
                var roomBytes = Read(path + 0x20, 8);
                var room = roomBytes is null ? 0 : BitConverter.ToUInt64(roomBytes);
                var drlgRoomBytes = Read(room + 0x18, 8);
                var drlgRoom = drlgRoomBytes is null ? 0 : BitConverter.ToUInt64(drlgRoomBytes);
                var levelBytes = Read(drlgRoom + 0x90, 8);
                var level = levelBytes is null ? 0 : BitConverter.ToUInt64(levelBytes);
                var levelId = Read(level + 0x1f8, 4);
                // Recheck identity after following transient room/path pointers.
                var identity = Read(unit, 0x10);
                if (identity is null || BitConverter.ToUInt32(identity) != 0 || BitConverter.ToUInt32(identity, 8) != id)
                    return Failed("Player identity changed during observation.");
                return new(state, localServerPointer, selector, id, pointer, act, levelId is null ? null : BitConverter.ToUInt32(levelId), levelId is null ? "Player level path is temporarily unavailable." : null);
            }
            unit = BitConverter.ToUInt64(header, 0x150);
        }
        return Failed(unit == 0 ? "Local player is absent from the client hash table." : "Player hash chain exceeds bounds or cycles.");
    }
    internal Readiness ReadReadiness()
    {
        var ready = Read(imageBase + 0x1cb410a, 1);
        var polls = Read(imageBase + 0x1df63c0, 8);
        var work = Read(imageBase + 0x1cd7020, 4);
        var managerBytes = Read(imageBase + 0x1c74b48, 8);
        var manager = managerBytes is null ? (ulong?)null : BitConverter.ToUInt64(managerBytes);
        var pending = manager is { } pointer && Pointer(pointer) ? Read(pointer + 8, 8) : null;
        return new(ready?[0], polls is null ? null : BitConverter.ToUInt64(polls), work is null ? null : BitConverter.ToInt32(work), manager is { } address ? $"0x{address:X}" : null, pending is null ? null : BitConverter.ToUInt64(pending));
    }
    internal Ui ReadUi()
    {
        var widgets = new List<VisibleWidget>();
        var loading = new HashSet<string>(StringComparer.Ordinal);
        var seen = new HashSet<ulong>();
        Ui Failed(string error) => new(widgets.ToArray(), loading.Order().ToArray(), seen.Count, error);
        var managerBytes = Read(imageBase + 0x1d7c4e8, 8);
        var manager = managerBytes is null ? 0 : BitConverter.ToUInt64(managerBytes);
        if (!Pointer(manager)) return Failed("Panel manager unavailable.");
        var pending = new Stack<(ulong Address, int Depth)>();
        pending.Push((manager, 0));
        while (pending.TryPop(out var node))
        {
            if (seen.Count >= 4096) return Failed("Widget node bound exceeded.");
            if (!seen.Add(node.Address)) return Failed("Widget tree cycles or contains duplicate nodes.");
            var header = Read(node.Address, 0x68);
            if (header is null) return Failed("Attached widget unreadable.");
            if (node.Depth != 0 && (header[0x50] == 0 || header[0x51] == 0)) continue;
            var namePointer = BitConverter.ToUInt64(header, 8);
            if (node.Depth != 0 && namePointer != 0)
            {
                var bytes = Read(namePointer, 96);
                if (bytes is null)
                {
                    var name = new List<byte>();
                    for (var i = 0; i < 96; i++)
                    {
                        var value = Read(namePointer + (ulong)i, 1);
                        if (value is null) return Failed("Visible widget name unreadable.");
                        name.Add(value[0]); if (value[0] == 0) break;
                    }
                    bytes = name.ToArray();
                }
                var end = Array.IndexOf(bytes, (byte)0);
                if (end < 0) return Failed("Widget name bound exceeded.");
                var nameText = Encoding.UTF8.GetString(bytes, 0, end);
                if (node.Depth == 1 || nameText.Contains("Panel", StringComparison.Ordinal) || nameText.Contains("Loading", StringComparison.OrdinalIgnoreCase) || nameText.Contains("Screen", StringComparison.Ordinal))
                    widgets.Add(new(nameText, node.Depth));
                if (nameText.Contains("Loading", StringComparison.OrdinalIgnoreCase) || nameText.Equals("LoadScreenPanel", StringComparison.Ordinal)) loading.Add(nameText);
            }
            var children = BitConverter.ToUInt64(header, 0x58);
            var count = BitConverter.ToUInt64(header, 0x60);
            if (count > 512) return Failed("Widget child count bound exceeded.");
            if (count == 0) continue;
            var childBytes = Read(children, checked((int)count * 8));
            if (childBytes is null) return Failed("Child array unreadable.");
            for (var i = 0; i < (int)count; i++)
            {
                var child = BitConverter.ToUInt64(childBytes, i * 8);
                if (!Pointer(child)) return Failed("Invalid attached child pointer.");
                pending.Push((child, node.Depth + 1));
            }
        }
        return new(widgets.OrderBy(x => x.Depth).ThenBy(x => x.Name, StringComparer.Ordinal).ToArray(), loading.Order().ToArray(), seen.Count, null);
    }
    internal static void SelfTest()
    {
        var memory = new Dictionary<ulong, byte[]>();
        const ulong image = 0x140000000, unit = 0x10000000, path = 0x20000000, room = 0x30000000, drlgRoom = 0x40000000, level = 0x50000000;
        void Put(ulong address, byte[] bytes) => memory[address] = bytes;
        byte[]? Reader(ulong address, int size)
        {
            foreach (var (start, bytes) in memory)
                if (address >= start && address - start <= (ulong)bytes.Length && (ulong)size <= (ulong)bytes.Length - (address - start)) return bytes.AsSpan((int)(address - start), size).ToArray();
            return null;
        }
        Put(image + 0x1dc76f8, [1]); Put(image + 0x1d442d8, BitConverter.GetBytes(0U)); Put(image + 0x1bf1e80, BitConverter.GetBytes(42U));
        Put(image + 0x1d442e0 + (42 & 0x7f) * 8, BitConverter.GetBytes(unit));
        var header = new byte[0x158]; BitConverter.GetBytes(42U).CopyTo(header, 8); header[0x18] = 3; BitConverter.GetBytes(path).CopyTo(header, 0x38); Put(unit, header);
        Put(path + 0x20, BitConverter.GetBytes(room)); Put(room + 0x18, BitConverter.GetBytes(drlgRoom)); Put(drlgRoom + 0x90, BitConverter.GetBytes(level)); Put(level + 0x1f8, BitConverter.GetBytes(103U));
        var snapshot = new GameSnapshotReader(Reader, image).ReadLocation();
        if (snapshot is not { ActZeroBased: 3, LevelId: 103, PlayerId: 42, Error: null }) throw new Exception("Native player location chain test failed.");
        Put(level + 0x1f8, BitConverter.GetBytes(109U)); header[0x18] = 4;
        if (new GameSnapshotReader(Reader, image).ReadLocation() is not { ActZeroBased: 4, LevelId: 109 }) throw new Exception("Act/level change test failed.");
        Put(image + 0x1d442d8, BitConverter.GetBytes(8U));
        if (new GameSnapshotReader(Reader, image).ReadLocation().LevelId is not null) throw new Exception("Invalid selector test failed.");
        Put(image + 0x1d442d8, BitConverter.GetBytes(0U)); Put(path + 0x20, BitConverter.GetBytes(0UL));
        if (new GameSnapshotReader(Reader, image).ReadLocation() is not { ActZeroBased: 4, LevelId: null, Error: not null }) throw new Exception("Transient room loss test failed.");
        if (new GameSnapshotReader((address, size) => Reader(address, size)?.Skip(1).ToArray(), image).ReadLocation().LevelId is not null) throw new Exception("Partial read test failed.");
        Put(image + 0x1cb410a, [1]); Put(image + 0x1df63c0, BitConverter.GetBytes(7UL)); Put(image + 0x1cd7020, BitConverter.GetBytes(3));
        Put(image + 0x1c74b48, BitConverter.GetBytes(0x60000000UL)); Put(0x60000008, BitConverter.GetBytes(0x100000002UL));
        if (new GameSnapshotReader(Reader, image).ReadReadiness() is not { NativeReadyByte1CB410A: 1, PendingWaitPolls1DF63C0: 7, OutstandingWork1CD7020: 3, QueueManagerPendingAt8: 0x100000002UL }) throw new Exception("Readiness globals/QWORD pending test failed.");
        Put(image + 0x1c74b48, BitConverter.GetBytes(0UL));
        if (new GameSnapshotReader(Reader, image).ReadReadiness() is not { QueueManager1C74B48: "0x0", QueueManagerPendingAt8: null }) throw new Exception("Null readiness manager test failed.");
        Console.WriteLine("PASS: seven synthetic passive location/readiness tests.");
    }
}
