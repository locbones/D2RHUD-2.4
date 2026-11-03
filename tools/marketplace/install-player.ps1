# Run from an extracted release bundle; private pairing files are supplied separately.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [Parameter(Mandatory)][string]$LauncherPath,
    [Parameter(Mandatory)][string]$PlayerPairing,
    [Parameter(Mandatory)][string]$AgentPairing,
    [string]$Python = (Get-Command python -ErrorAction Stop).Source,
    [string]$PackageDirectory = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
if (Get-Process D2R,D2R_Debug,D2R_Debug_NoErrors,Pandemonium -ErrorAction SilentlyContinue) { throw 'Close D2R and Pandemonium before installing.' }
$manifest = Get-Content -LiteralPath (Join-Path $PackageDirectory 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.schema -ne 1 -or $manifest.game_mode -ne '2.4' -or $manifest.mod -ne 'RMD-MP') { throw 'Unsupported marketplace release.' }
foreach ($file in $manifest.files.PSObject.Properties) {
    $path = [IO.Path]::GetFullPath((Join-Path $PackageDirectory $file.Name))
    $root = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd('\') + '\'
    if (-not $path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.Value) { throw "Release verification failed: $($file.Name)" }
}
$player = Get-Content -LiteralPath $PlayerPairing -Raw | ConvertFrom-Json
$agent = Get-Content -LiteralPath $AgentPairing -Raw | ConvertFrom-Json
if ($player.role -ne 'player' -or $player.id -ne $agent.owner) { throw 'Player and agent pairings must identify the same player.' }
$base = Join-Path $env:USERPROFILE '.d2r-marketplace'
$upgradeLease = $null
try {
if (Test-Path -LiteralPath (Join-Path $base 'custody-enrollment.json')) {
    $upgrade = Get-Content -LiteralPath (Join-Path $base 'custody-enrollment.json') -Raw | ConvertFrom-Json
    $upgradeLease = [IO.File]::Open((Join-Path $upgrade.state_directory 'guard.lock'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    if (Test-Path -LiteralPath (Join-Path $upgrade.state_directory 'pending.json')) { throw 'Recover the pending transfer before upgrading.' }
    foreach ($entry in @(@{path=(Join-Path $upgrade.state_directory 'worker.json'); field='script'; pattern='*--watch*'}, @{path=(Join-Path $base 'sync-SC.json'); field='entrypoint'; pattern='*-Watch*'}, @{path=(Join-Path $base 'sync-HC.json'); field='entrypoint'; pattern='*-Watch*'})) {
        if (-not (Test-Path -LiteralPath $entry.path)) { continue }
        $record = Get-Content -LiteralPath $entry.path -Raw | ConvertFrom-Json
        $process = Get-Process -Id $record.pid -ErrorAction SilentlyContinue
        if (-not $process) { continue }
        $details = Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)"
        $script = $record.($entry.field)
        if ($script -and $process.StartTime.ToUniversalTime().Ticks -eq [long]$record.started_ticks -and $details.CommandLine -like "*$script*" -and $details.CommandLine -like $entry.pattern) {
            Stop-Process -Id $record.pid
            $process.WaitForExit(5000) | Out-Null
        }
    }
}
if (Test-Path -LiteralPath (Join-Path $base 'custody\pending.json')) { throw 'Recover pending custody before installing.' }
if (Test-Path -LiteralPath (Join-Path $base 'pairing.json')) {
    $previous = Get-Content -LiteralPath (Join-Path $base 'pairing.json') -Raw | ConvertFrom-Json
    if ($previous.id -ne $player.id) { throw 'This Windows profile is already paired to another player.' }
}
New-Item -ItemType Directory -Path $base -Force | Out-Null
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
& icacls.exe $base /inheritance:r /grant:r "*$($sid):(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot protect private player credentials.' }
Copy-Item -LiteralPath $PlayerPairing -Destination (Join-Path $base 'pairing.json') -Force
$sync = @{endpoint='https://market.d2rmodding.com/index.php'; player_id=$player.id; token_env='D2_MARKET_TOKEN'; mod='RMD-MP'; custody_status=(Join-Path $base 'custody\status.json')}
[IO.File]::WriteAllText((Join-Path $base 'sync-config.json'), ($sync | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
$backup = Join-Path $base ('backups\installation-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
foreach ($path in @((Join-Path $GameDirectory 'd2rhud.dll'),(Join-Path $GameDirectory 'lootfilter.lua'),$LauncherPath)) {
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $backup ([IO.Path]::GetFileName($path))) }
}
Copy-Item -LiteralPath (Join-Path $PackageDirectory 'game\d2rhud.dll') -Destination (Join-Path $GameDirectory 'd2rhud.dll') -Force
if ($manifest.files.'game/lootfilter.lua') {
    # Engine only: the player's lootfilter_config.lua and override rules remain intact.
    Copy-Item -LiteralPath (Join-Path $PackageDirectory 'game\lootfilter.lua') -Destination (Join-Path $GameDirectory 'lootfilter.lua') -Force
}
Copy-Item -LiteralPath (Join-Path $PackageDirectory 'launcher\Pandemonium.exe') -Destination $LauncherPath -Force
$tools = Join-Path $PackageDirectory 'tools'
& $Python (Join-Path $tools 'install-native-layout.py') (Join-Path $GameDirectory 'Mods\RMD-MP\RMD-MP.mpq\data')
if ($LASTEXITCODE -ne 0) { throw 'Native layout installation failed; original binaries are backed up.' }
& (Join-Path $tools 'install-custody.ps1') -GameDirectory $GameDirectory -LauncherPath $LauncherPath -AgentPairing $AgentPairing -HelperPath (Join-Path $tools 'custody-save.exe') -Python $Python -HudSha256 $manifest.files.'game/d2rhud.dll' -LauncherSha256 $manifest.files.'launcher/Pandemonium.exe'
if (-not $?) { throw 'Custody enrollment failed; keep the background agent stopped.' }
& (Join-Path $base 'custody\runtime\start-marketplace.ps1') -InstallStartup
Write-Output 'Installed. Launch RMD-MP through Pandemonium, choose deposit/delivery pages in Marketplace Setup, then save.'
} finally { if ($upgradeLease) { $upgradeLease.Dispose() } }
