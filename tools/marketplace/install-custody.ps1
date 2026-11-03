[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [Parameter(Mandatory)][string]$LauncherPath,
    [Parameter(Mandatory)][string]$AgentPairing,
    [Parameter(Mandatory)][string]$HelperPath,
    [Parameter(Mandatory)][string]$HudSha256,
    [Parameter(Mandatory)][string]$LauncherSha256,
    [string]$SaveFolder = (Join-Path $env:USERPROFILE 'Saved Games\Diablo II Resurrected\mods\RMD-MP'),
    [string]$Python = (Get-Command python -ErrorAction Stop).Source
)
$ErrorActionPreference = 'Stop'
if (Get-Process D2R,D2R_Debug,D2R_Debug_NoErrors,Pandemonium -ErrorAction SilentlyContinue) { throw 'Close the game and launcher before enrolling custody.' }
$base = Join-Path $env:USERPROFILE '.d2r-marketplace'
$state = Join-Path $base 'custody'
$runtime = Join-Path $state 'runtime'
if (Test-Path -LiteralPath (Join-Path $state 'pending.json')) { throw 'Recover the pending custody operation before reinstalling.' }
if ((Get-FileHash -LiteralPath (Join-Path $GameDirectory 'd2rhud.dll') -Algorithm SHA256).Hash -ne $HudSha256 -or
    (Get-FileHash -LiteralPath $LauncherPath -Algorithm SHA256).Hash -ne $LauncherSha256) { throw 'Install and verify the launcher and HUD from this custody release first.' }
$pairing = Get-Content -LiteralPath $AgentPairing -Raw | ConvertFrom-Json
$player = Get-Content -LiteralPath (Join-Path $base 'pairing.json') -Raw | ConvertFrom-Json
if ($pairing.owner -ne $player.id -or $player.role -ne 'player') { throw 'The save agent must belong to this computer''s player pairing.' }
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
& icacls.exe $state /inheritance:r /grant:r "*$($sid):(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot protect private custody recovery files.' }
foreach ($name in @('custody-agent.py','custody-workspace.py','sync-marketplace.py','sync-dreamhost.ps1','start-marketplace-sync.ps1','stop-marketplace-sync.ps1','start-custody-agent.ps1','start-marketplace.ps1','background-marketplace.py','open-marketplace.ps1','revalidate-custody.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $runtime $name) -Force
}
Copy-Item -LiteralPath $HelperPath -Destination (Join-Path $runtime 'custody-save.exe') -Force
Copy-Item -LiteralPath $AgentPairing -Destination (Join-Path $state 'agent-pairing.json') -Force
$data = Join-Path $GameDirectory 'Mods\RMD-MP\RMD-MP.mpq\data'
$probe = @'
import runpy,sys,json
a=runpy.run_path(sys.argv[1])
print(json.dumps({'catalog':a['catalog'](sys.argv[2]),'helper_sha256':a['io'].digest(a['io'].read(sys.argv[3]))}))
'@
$verified = & $Python -c $probe (Join-Path $runtime 'custody-agent.py') $data (Join-Path $runtime 'custody-save.exe') | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $verified.catalog -ne $pairing.catalog) { throw 'The agent pairing does not match the installed compiled item/stat catalog.' }
$backup = Join-Path $state ('backups\enrollment-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Get-ChildItem -LiteralPath $SaveFolder -File | Where-Object { $_.Name -match '^Stash_(SC|HC)_Page\d+\.d2i$|^Marketplace_2\.4_(SC|HC)\.json$' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $backup $_.Name)
}
if (-not (Test-Path -LiteralPath (Join-Path $state 'guard.lock'))) { [IO.File]::WriteAllText((Join-Path $state 'guard.lock'), '') }
$config = @{
    schema=1; enrolled=$true; owner=$player.id; endpoint='https://market.d2rmodding.com/index.php';
    state_directory=$state; save_directory=[IO.Path]::GetFullPath($SaveFolder); game_directory=[IO.Path]::GetFullPath($GameDirectory);
    mod_data=$data; helper=(Join-Path $runtime 'custody-save.exe'); helper_sha256=$verified.helper_sha256; catalog=$verified.catalog;
    agent_pairing=(Join-Path $state 'agent-pairing.json'); python=$Python; agent_script=(Join-Path $runtime 'custody-agent.py');
    gold_cap=2500000; launcher_path=$LauncherPath; hud_sha256=$HudSha256; launcher_sha256=$LauncherSha256
}
$enrollment = Join-Path $base 'custody-enrollment.json'
$temporary = $enrollment + '.new'
[IO.File]::WriteAllText($temporary, ($config | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporary -Destination $enrollment -Force
$shortcut = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Marketplace website.lnk'
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcut)
$link.TargetPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$link.Arguments = '-NoProfile -WindowStyle Hidden -File "' + (Join-Path $runtime 'open-marketplace.ps1') + '"'
$link.WorkingDirectory = $runtime
$link.Description = 'Open the community marketplace with your game player account.'
$link.Save()
Write-Output "Custody enrolled. Recovery backup: $backup. The background agent has not been started."
