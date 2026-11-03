using System.Text;

// Layout is the noncreating attached-widget traversal already used by QuestJournal::PollMenus.
// No native UI function is called: FindPanel would create absent panels.
static class NativeFrontendObserver
{
    internal sealed record Snapshot(bool Ready, string? PanelName, bool LoadingPanelVisible, int VisitedNodes, string? Error);
    internal static Snapshot Observe(Func<ulong, int, byte[]?> read, ulong imageBase)
    {
        const int nodeLimit = 4096, childrenLimit = 512, nameLimit = 96;
        var visited = new HashSet<ulong>();
        string? frontend = null;
        var loading = false;
        Snapshot Failed(string error) => new(false, frontend, loading, visited.Count, error);
        static bool Pointer(ulong value) => value >= 0x10000 && value < 0x0000800000000000;
        byte[]? Exact(ulong address, int length)
        {
            if (!Pointer(address) || length is < 1 or > 4096 || address + (ulong)length < address) return null;
            var bytes = read(address, length);
            return bytes?.Length == length ? bytes : null;
        }
        var rootBytes = Exact(imageBase + 0x1d7c4e8, 8);
        if (rootBytes is null) return Failed("Panel manager global is unreadable.");
        var root = BitConverter.ToUInt64(rootBytes);
        if (!Pointer(root)) return Failed("Panel manager is not attached.");
        var pending = new Stack<(ulong Address, bool Root)>();
        pending.Push((root, true));
        while (pending.Count != 0)
        {
            if (visited.Count >= nodeLimit) return Failed("Widget node limit exceeded.");
            var (address, isRoot) = pending.Pop();
            if (!visited.Add(address)) return Failed("Widget tree contains a cycle or duplicate node.");
            var header = Exact(address, 0x68);
            if (header is null) return Failed("Attached widget is unreadable.");
            if (!isRoot && (header[0x50] == 0 || header[0x51] == 0)) continue;
            if (!isRoot && BitConverter.ToUInt64(header, 8) != 0)
            {
                var nameAddress = BitConverter.ToUInt64(header, 8);
                var nameBytes = Exact(nameAddress, nameLimit);
                if (nameBytes is null)
                {
                    // A valid C string can finish at a readable page boundary.
                    var name = new List<byte>();
                    for (var i = 0; i < nameLimit; i++)
                    {
                        var value = Exact(nameAddress + (ulong)i, 1);
                        if (value is null) return Failed("Visible widget name is unreadable.");
                        name.Add(value[0]);
                        if (value[0] == 0) break;
                    }
                    nameBytes = name.ToArray();
                }
                var terminator = Array.IndexOf(nameBytes, (byte)0);
                if (terminator < 0) return Failed("Visible widget name exceeds its bound.");
                var text = Encoding.UTF8.GetString(nameBytes, 0, terminator);
                if (text is "MainMenuPanel" or "CharacterSelectPanel") frontend ??= text;
                if (text == "MainMenuLoadingPanel") loading = true;
            }
            var children = BitConverter.ToUInt64(header, 0x58);
            var count = BitConverter.ToUInt64(header, 0x60);
            if (count > childrenLimit) return Failed("Attached child count exceeds its bound.");
            if (count == 0) continue;
            var pointers = Exact(children, checked((int)count * 8));
            if (pointers is null) return Failed("Attached child array is unreadable.");
            for (var i = 0; i < (int)count; i++)
            {
                var child = BitConverter.ToUInt64(pointers, i * 8);
                if (!Pointer(child)) return Failed("Attached child pointer is invalid.");
                pending.Push((child, false));
            }
        }
        return new(frontend is not null && !loading, frontend, loading, visited.Count, null);
    }

    internal static void SelfTest()
    {
        var memory = new Dictionary<ulong, byte[]>();
        ulong next = 0x10000000;
        const ulong imageBase = 0x140000000;
        ulong Allocate(byte[] bytes) { var address = next; next += (ulong)bytes.Length + 0x100; memory[address] = bytes; return address; }
        byte[]? Read(ulong address, int length)
        {
            foreach (var (start, bytes) in memory)
                if (address >= start && address - start <= (ulong)bytes.Length && (ulong)length <= (ulong)bytes.Length - (address - start))
                    return bytes.AsSpan((int)(address - start), length).ToArray();
            return null;
        }
        ulong Widget(string name, bool enabled = true, bool visible = true, params ulong[] children)
        {
            var bytes = new byte[0x68];
            BitConverter.GetBytes(Allocate(Encoding.UTF8.GetBytes(name + '\0'))).CopyTo(bytes, 8);
            bytes[0x50] = enabled ? (byte)1 : (byte)0;
            bytes[0x51] = visible ? (byte)1 : (byte)0;
            if (children.Length != 0) BitConverter.GetBytes(Allocate(children.SelectMany(BitConverter.GetBytes).ToArray())).CopyTo(bytes, 0x58);
            BitConverter.GetBytes((ulong)children.Length).CopyTo(bytes, 0x60);
            return Allocate(bytes);
        }
        void Root(ulong root) => memory[imageBase + 0x1d7c4e8] = BitConverter.GetBytes(root);
        var checks = 0;
        void Check(bool value, string name) { checks++; if (!value) throw new InvalidOperationException("Native frontend self-test failed: " + name); }
        var menu = Widget("MainMenuPanel");
        var character = Widget("CharacterSelectPanel");
        Root(Widget("Manager", false, false, menu));
        Check(Observe(Read, imageBase) is { Ready: true, PanelName: "MainMenuPanel" }, "visible menu; manager flags exempt");
        Root(Widget("Manager", true, true, character));
        Check(Observe(Read, imageBase) is { Ready: true, PanelName: "CharacterSelectPanel" }, "character select");
        Root(Widget("Manager", true, true, Widget("Ancestor", true, false, menu)));
        Check(!Observe(Read, imageBase).Ready, "hidden ancestor");
        Root(Widget("Manager", true, true, Widget("Ancestor", false, true, menu)));
        Check(!Observe(Read, imageBase).Ready, "disabled ancestor");
        Root(Widget("Manager", true, true, menu, Widget("MainMenuLoadingPanel")));
        Check(Observe(Read, imageBase) is { Ready: false, LoadingPanelVisible: true }, "loading overlay");
        Root(Widget("Manager", true, true, menu, Widget("MainMenuLoadingPanel", true, false)));
        Check(Observe(Read, imageBase).Ready, "hidden loading overlay");
        Root(Widget("Manager", true, true, Widget("MainMenuPanelAlmost")));
        Check(!Observe(Read, imageBase).Ready, "exact C string match");
        Root(Widget("Manager", true, true, Widget("Unknown")));
        Check(!Observe(Read, imageBase).Ready, "unknown visible panel");
        Root(Widget("Manager", true, true, 0x1234));
        Check(Observe(Read, imageBase) is { Ready: false, Error: not null }, "invalid child pointer");
        var cycle = Widget("Cycle");
        BitConverter.GetBytes(Allocate(BitConverter.GetBytes(cycle))).CopyTo(memory[cycle], 0x58);
        BitConverter.GetBytes(1UL).CopyTo(memory[cycle], 0x60);
        Root(cycle);
        Check(Observe(Read, imageBase) is { Ready: false, Error: not null }, "cycle bounded");
        var large = Widget("Manager");
        BitConverter.GetBytes(513UL).CopyTo(memory[large], 0x60);
        Root(large);
        Check(Observe(Read, imageBase) is { Ready: false, Error: not null }, "child count bound");
        Root(0);
        Check(Observe(Read, imageBase) is { Ready: false, Error: not null }, "null manager");
        Root(Widget("Manager", true, true, menu));
        Check(!Observe((address, length) => Read(address, length)?.Skip(1).ToArray(), imageBase).Ready, "partial memory reads rejected");
        var nameless = Widget("", true, true, menu);
        BitConverter.GetBytes(0UL).CopyTo(memory[nameless], 8);
        Root(Widget("Manager", true, true, nameless));
        Check(Observe(Read, imageBase).Ready, "nameless ancestor still traverses children");
        Console.WriteLine($"PASS: {checks} synthetic native frontend checks.");
    }
}
