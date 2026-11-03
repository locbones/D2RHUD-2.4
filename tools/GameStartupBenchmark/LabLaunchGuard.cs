static class LabLaunchGuard
{
    internal static void RequirePreparedFiles(string gameDirectory)
    {
        var game = Path.GetFullPath(gameDirectory).TrimEnd(Path.DirectorySeparatorChar);
        using var manifest = System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(game, "lab-manifest.json")));
        var root = manifest.RootElement;
        var recordedGame = Path.GetFullPath(root.GetProperty("GameRoot").GetString()!);
        var save = Path.GetFullPath(root.GetProperty("SaveDirectory").GetString()!);
        var sourceSave = Path.GetFullPath(root.GetProperty("SourceSaveDirectory").GetString()!);
        var saveName = root.GetProperty("PrivateSavePathValue").GetString()!;
        var modInfo = File.ReadAllText(Path.Combine(game, "mods", "RMD-MP", "RMD-MP.mpq", "modinfo.json"));
        var matches = System.Text.RegularExpressions.Regex.Matches(modInfo, "\"savepath\"\\s*:\\s*\"([^\"\\r\\n]*)\"");
        if (!recordedGame.Equals(game, StringComparison.OrdinalIgnoreCase) || save.Equals(sourceSave, StringComparison.OrdinalIgnoreCase) ||
            !saveName.StartsWith("RMD-MP-ActLoadLab-", StringComparison.Ordinal) || saveName.Contains("..", StringComparison.Ordinal) ||
            !Path.GetFileName(save).Equals(saveName.TrimEnd('/'), StringComparison.Ordinal) || matches.Count != 1 || matches[0].Groups[1].Value != saveName)
            throw new InvalidOperationException("Prepared lab manifest/modinfo does not identify an independent copied-save directory.");
        if (!Directory.Exists(save) || !Directory.Exists(Path.Combine(game, "LocalAppData")))
            throw new InvalidOperationException("Prepared private save/LocalAppData directory is missing.");
        if ((File.GetAttributes(Path.Combine(game, "D2R.exe")) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidOperationException("Prepared lab EXE must be an independent private file.");
    }
    internal static void Require(string gameDirectory, string? localAppData, string temporaryDirectory)
    {
        var game = Path.GetFullPath(gameDirectory).TrimEnd(Path.DirectorySeparatorChar);
        var temporary = Path.GetFullPath(temporaryDirectory).TrimEnd(Path.DirectorySeparatorChar);
        var relative = Path.GetRelativePath(temporary, game);
        if (relative.Contains(Path.DirectorySeparatorChar) || relative.Contains(Path.AltDirectorySeparatorChar) ||
            !relative.StartsWith("D2RHUD-act-load-lab-", StringComparison.OrdinalIgnoreCase) || relative.Length <= "D2RHUD-act-load-lab-".Length)
            throw new InvalidOperationException("Keep-open mode requires a dedicated TEMP/D2RHUD-act-load-lab-<id> game directory.");
        if (string.IsNullOrWhiteSpace(localAppData) || !Path.GetFullPath(localAppData).TrimEnd(Path.DirectorySeparatorChar).Equals(Path.Combine(game, "LocalAppData"), StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Keep-open mode requires the launching process LOCALAPPDATA to equal the lab's private LocalAppData directory.");
        if (Directory.Exists(game) && (File.GetAttributes(game) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidOperationException("Keep-open lab root must be an independent directory, not a junction or symlink.");
    }
    internal static void SelfTest()
    {
        var temporary = Path.GetTempPath();
        var game = Path.Combine(temporary, "D2RHUD-act-load-lab-selftest-" + Guid.NewGuid().ToString("N"));
        Require(game, Path.Combine(game, "LocalAppData"), temporary);
        var checks = 1;
        void Reject(Action action)
        {
            try { action(); } catch (InvalidOperationException) { checks++; return; }
            throw new Exception("Keep-open guard accepted an unsafe directory/environment combination.");
        }
        Reject(() => Require(Path.Combine(temporary, "ordinary-game"), Path.Combine(game, "LocalAppData"), temporary));
        Reject(() => Require(Path.Combine(game, "nested"), Path.Combine(game, "nested", "LocalAppData"), temporary));
        Reject(() => Require(game, temporary, temporary));
        Reject(() => Require(game, null, temporary));
        Reject(() => Require(Path.Combine(temporary, "D2RHUD-act-load-lab-"), Path.Combine(temporary, "D2RHUD-act-load-lab-", "LocalAppData"), temporary));
        Console.WriteLine($"PASS: {checks} keep-open lab guard checks.");
    }
}
