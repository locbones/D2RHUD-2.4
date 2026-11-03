# One-shot sync using this computer's private player pairing. No tokens are
# stored in the repository or game saves, or printed on the command line.
[CmdletBinding()]
param(
    [string]$SaveFolder = (Join-Path $env:USERPROFILE 'Saved Games\Diablo II Resurrected\mods\RMD-MP'),
    [switch]$Hardcore,
    [switch]$Watch
)
$ErrorActionPreference = 'Stop'
$pairingDirectory = Join-Path $env:USERPROFILE '.d2r-marketplace'
$pairing = Get-Content -LiteralPath (Join-Path $pairingDirectory 'pairing.json') -Raw | ConvertFrom-Json
$configPath = Join-Path $pairingDirectory 'sync-config.json'
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if ($pairing.id -ne $config.player_id -or $pairing.role -ne 'player' -or $config.endpoint -ne 'https://market.d2rmodding.com/index.php') {
    throw 'The private pairing does not match this marketplace.'
}
if (-not (Test-Path -LiteralPath $SaveFolder -PathType Container)) {
    throw "Save folder does not exist: $SaveFolder"
}
$hadToken = Test-Path Env:D2_MARKET_TOKEN
$previousToken = [Environment]::GetEnvironmentVariable('D2_MARKET_TOKEN', 'Process')
try {
    $env:D2_MARKET_TOKEN = $pairing.token
    $syncOptions = @()
    if ($Hardcore) { $syncOptions += '--hardcore' }
    if ($Watch) { $syncOptions += '--watch' }
    & python (Join-Path $PSScriptRoot 'sync-marketplace.py') $configPath $SaveFolder @syncOptions
    if ($LASTEXITCODE -ne 0) { throw 'Marketplace sync failed. Check the connection error above.' }
    Write-Host 'Marketplace cache updated. Use Refresh in the game.'
} finally {
    if ($hadToken) {
        [Environment]::SetEnvironmentVariable('D2_MARKET_TOKEN', $previousToken, 'Process')
    } else {
        Remove-Item Env:D2_MARKET_TOKEN -ErrorAction SilentlyContinue
    }
}
