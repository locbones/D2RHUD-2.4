param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$SaveRoot = (Join-Path $env:USERPROFILE 'Saved Games/Diablo II Resurrected/Mods'),
    [string]$OutputParent = $env:TEMP
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
$sourceSave = [IO.Path]::GetFullPath((Join-Path $SaveRoot 'RMD-MP'))
if (Get-Process D2R -ErrorAction SilentlyContinue) { throw 'Close D2R before creating a consistent isolated character snapshot.' }
$sourceModInfo = Join-Path $sourceRoot 'mods/RMD-MP/RMD-MP.mpq/modinfo.json'
$modInfo = [IO.File]::ReadAllText($sourceModInfo)
$saveMatch = [regex]::Matches($modInfo, '"savepath"\s*:\s*"([^"\r\n]*)"')
if ($saveMatch.Count -ne 1 -or $saveMatch[0].Groups[1].Value.TrimEnd('/','\') -ne 'RMD-MP') { throw 'Unexpected mod savepath; refusing to infer a source save directory.' }
$character = Get-ChildItem -LiteralPath $sourceSave -File -Filter '*.d2s' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (!$character) { throw 'No source character exists.' }
$id = [guid]::NewGuid().ToString('N').Substring(0,10)
$labRoot = [IO.Path]::GetFullPath((Join-Path $OutputParent "D2RHUD-act-load-lab-$id"))
$labSaveName = "RMD-MP-ActLoadLab-$id"
$labSave = [IO.Path]::GetFullPath((Join-Path $SaveRoot $labSaveName))
if ((Test-Path -LiteralPath $labRoot) -or (Test-Path -LiteralPath $labSave)) { throw 'Unique lab directory already exists.' }
$labAppData = Join-Path $labRoot 'LocalAppData'
$backupRoot = Join-Path $labRoot 'character-source-backup'
$junctions = [Collections.Generic.List[object]]::new()
$copies = [Collections.Generic.List[object]]::new()
$sourceHashes = [Collections.Generic.List[object]]::new()
New-Item -ItemType Directory -Path $labRoot,$labSave,$labAppData,$backupRoot | Out-Null
function Assert-LabTarget([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (!$full.StartsWith($labRoot + '\',[StringComparison]::OrdinalIgnoreCase) -and !$full.StartsWith($labSave + '\',[StringComparison]::OrdinalIgnoreCase)) { throw "Target is outside the two new lab roots: $full" }
}
function Copy-PrivateFile([string]$Source,[string]$Destination,[bool]$ProtectSource = $false) {
    Assert-LabTarget $Destination
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
    [IO.File]::Copy($Source,$Destination,$false)
    $hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $hash) { throw "Private copy hash mismatch: $Destination" }
    $copies.Add([pscustomobject]@{Source=$Source;Destination=$Destination;Sha256=$hash})
    if ($ProtectSource) { $sourceHashes.Add([pscustomobject]@{Path=$Source;Sha256=$hash}) }
}
function Copy-PrivateTree([string]$Source,[string]$Destination) {
    Assert-LabTarget (Join-Path $Destination '.target-check')
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    foreach ($entry in Get-ChildItem -LiteralPath $Source -Force) {
        if ($entry.Name -match '\.(bak|dmp)$|\.bak\.|\.dmp\.|\.old$') { continue }
        if ($entry.PSIsContainer) { Copy-PrivateTree $entry.FullName (Join-Path $Destination $entry.Name) }
        else { Copy-PrivateFile $entry.FullName (Join-Path $Destination $entry.Name) }
    }
}
function Link-StaticTree([string]$Source,[string]$Destination) {
    Assert-LabTarget $Destination
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
    New-Item -ItemType Junction -Path $Destination -Target $Source | Out-Null
    $junctions.Add([pscustomobject]@{Path=$Destination;Target=$Source;Purpose='Shared static assets or CASC, used for reads only';EnforcedReadOnly=$false;CleanupRule='Remove junction entry itself only; NEVER recursively delete or traverse this path for cleanup'})
}
function Copy-DataLayout([string]$Source,[string]$Destination) {
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    foreach ($entry in Get-ChildItem -LiteralPath $Source -Force) {
        $target = Join-Path $Destination $entry.Name
        if (!$entry.PSIsContainer) { Copy-PrivateFile $entry.FullName $target; continue }
        if ($entry.Name -eq 'global') {
            [IO.Directory]::CreateDirectory($target) | Out-Null
            foreach ($global in Get-ChildItem -LiteralPath $entry.FullName -Force) {
                $globalTarget = Join-Path $target $global.Name
                if (!$global.PSIsContainer) { Copy-PrivateFile $global.FullName $globalTarget }
                elseif ($global.Name -in @('excel','ui','commands')) { Copy-PrivateTree $global.FullName $globalTarget }
                else { Link-StaticTree $global.FullName $globalTarget }
            }
        }
        elseif ($entry.Name -in @('data','indices','hd','local','ReMoDDeD')) { Link-StaticTree $entry.FullName $target }
        else { Copy-PrivateTree $entry.FullName $target }
    }
}
# Whitelist active runtime/config files, excluding old builds, dumps, logs and the
# absolute-path active-filter receipt. File.Copy follows the source EXE link into a real private file.
foreach ($entry in Get-ChildItem -LiteralPath $sourceRoot -File -Force) {
    if ($entry.Name -eq '.build.info' -or $entry.Name -in @('D2R.exe','d2rhud.dll','d3d12.dll','dxilconv7.dll','nvngx_dlss.dll','Exocet.otf','lootfilter.lua','lootfilter_config.lua','override_rules.lua','HUDConfig_RMD-MP.json')) {
        Copy-PrivateFile $entry.FullName (Join-Path $labRoot $entry.Name) $true
    }
}
if ((Get-Item -LiteralPath (Join-Path $labRoot 'D2R.exe')).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Private D2R.exe must be a real independent file.' }
Copy-DataLayout (Join-Path $sourceRoot 'data') (Join-Path $labRoot 'data')
$labMod = Join-Path $labRoot 'mods/RMD-MP/RMD-MP.mpq'
[IO.Directory]::CreateDirectory($labMod) | Out-Null
Copy-DataLayout (Join-Path $sourceRoot 'mods/RMD-MP/RMD-MP.mpq/data') (Join-Path $labMod 'data')
$privateModInfo = [regex]::Replace($modInfo, '"savepath"\s*:\s*"[^"\r\n]*"', '"savepath": "' + $labSaveName + '/"')
[IO.File]::WriteAllText((Join-Path $labMod 'modinfo.json'),$privateModInfo,[Text.UTF8Encoding]::new($false))
$sourceHashes.Add([pscustomobject]@{Path=$sourceModInfo;Sha256=(Get-FileHash -LiteralPath $sourceModInfo).Hash})
if (Test-Path -LiteralPath (Join-Path $sourceRoot 'My Filters')) { Copy-PrivateTree (Join-Path $sourceRoot 'My Filters') (Join-Path $labRoot 'My Filters') }
$characterFiles = @(Get-ChildItem -LiteralPath $sourceSave -File | Where-Object { $_.BaseName -eq $character.BaseName -and $_.Extension -match '^\.(d2s|ctl|ctlo|key|map|ma\d+)$' })
foreach ($entry in $characterFiles) {
    Copy-PrivateFile $entry.FullName (Join-Path $labSave $entry.Name) $true
    Copy-PrivateFile $entry.FullName (Join-Path $backupRoot $entry.Name)
}
if (Test-Path -LiteralPath (Join-Path $sourceSave 'Settings.json')) { Copy-PrivateFile (Join-Path $sourceSave 'Settings.json') (Join-Path $labSave 'Settings.json') $true }
# Rewrite only private root configuration references to the old game folder.
$privateHud = Join-Path $labRoot 'HUDConfig_RMD-MP.json'
$hudText = [IO.File]::ReadAllText($privateHud).Replace($sourceRoot.Replace('\','\\'),$labRoot.Replace('\','\\')).Replace($sourceRoot.Replace('\','/'),$labRoot.Replace('\','/'))
[IO.File]::WriteAllText($privateHud,$hudText,[Text.UTF8Encoding]::new($false))
$launcherSource = Join-Path $env:LOCALAPPDATA 'Pandemonium/settings.json'
$launcher = Get-Content -LiteralPath $launcherSource -Raw | ConvertFrom-Json
$launcher.Version24.GameDirectory = $labRoot
$launcher.Version24.SaveDirectory = $labSave
$launcher.Version24.BackupDirectory = Join-Path $labRoot 'save-backups'
$launcher.Version24.DisableUpdateChecks = $true
$launcher.Version24.LastPlayedModId = 'RMD-MP'
$launcher.Version24.LootFilterAutoUpdates = $false
if ($launcher.Version24.ModProfiles.'RMD-MP') { $launcher.Version24.ModProfiles.'RMD-MP'.LootFilterAutoUpdates = $false }
$privateSettings = Join-Path $labRoot 'launcher-settings.json'
$launcher | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $privateSettings -Encoding utf8
$sourceHashes.Add([pscustomobject]@{Path=$launcherSource;Sha256=(Get-FileHash -LiteralPath $launcherSource).Hash})
$manifest = [pscustomobject]@{
    SchemaVersion=1;LabId=$id;CreatedUtc=[DateTime]::UtcNow;GameRoot=$labRoot;SourceGameRoot=$sourceRoot;ModId='RMD-MP';PrivateModInfo=(Join-Path $labMod 'modinfo.json');PrivateSavePathValue=($labSaveName+'/');SaveDirectory=$labSave;SourceSaveDirectory=$sourceSave;Character=$character.Name;CharacterBackupDirectory=$backupRoot;LocalAppData=$labAppData;LauncherSettings=$privateSettings;
    LaunchEnvironment=@{LOCALAPPDATA=$labAppData};EnvironmentRule='Set LOCALAPPDATA on the launching benchmark/launcher process itself; native CreateProcess inherits it. A ProcessStartInfo-only override is insufficient in the current Windows launcher.';
    Junctions=$junctions;CopiedFiles=$copies;CopiedFileHashMeaning='Sha256 is source content, verified against destination when copied. The private HUD configuration is subsequently rewritten to private paths; final generated-file hashes are recorded separately.';ProtectedSourceHashes=$sourceHashes;SharedStashCopied=$false;Launched=$false;
    PrivateGeneratedFileHashes=@($privateHud,(Join-Path $labMod 'modinfo.json'),$privateSettings | ForEach-Object { [pscustomobject]@{Path=$_;Sha256=(Get-FileHash -LiteralPath $_).Hash} });
    CleanupRule='Contains junctions to original data. NEVER recursively delete this tree through junctions. Remove each listed junction entry itself before removing lab-owned directories.';
    StaticAssetPolicy='Junctions are NOT filesystem-enforced read-only. Game code is expected to read static asset/CASC targets. All known mutable Excel/UI/Pandemonium/D2RHUD/config/save locations are private copies.'
}
$manifestPath = Join-Path $labRoot 'lab-manifest.json'
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding utf8
foreach ($protected in $sourceHashes) { if ((Get-FileHash -LiteralPath $protected.Path).Hash -ne $protected.Sha256) { throw "Original source changed during preparation: $($protected.Path)" } }
[pscustomobject]@{GameRoot=$labRoot;SaveDirectory=$labSave;Character=$character.Name;LocalAppData=$labAppData;LauncherSettings=$privateSettings;Manifest=$manifestPath;Junctions=$junctions.Count;CopiedFiles=$copies.Count;Launched=$false} | ConvertTo-Json
