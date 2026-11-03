param(
    [Parameter(Mandatory=$true)][string]$ModData,
    [Parameter(Mandatory=$true)][string]$ReferenceSaves,
    [string]$Php = "$PSScriptRoot/../build-marketplace/php-runtime/php.exe",
    [string]$Helper = "$PSScriptRoot/../build-marketplace/tools/custody-save.exe"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/..").Path
$results = @()
function Invoke-ResilienceCheck([string]$Name,[string]$Program,[string[]]$Arguments) {
    $started = Get-Date
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE" }
    $script:results += [ordered]@{ check=$Name; passed=$true; seconds=[Math]::Round(((Get-Date)-$started).TotalSeconds,2) }
}
Push-Location $repo
try {
    $phpPath = (Resolve-Path $Php).Path
    $helperPath = (Resolve-Path $Helper).Path
    $phpArgs = @('-d',"extension_dir=$(Split-Path $phpPath)/ext",'-d','extension=pdo_sqlite')
    foreach ($suite in @('bundles','experience','escrow','wallet','transfers','identity')) {
        Invoke-ResilienceCheck $suite $phpPath ($phpArgs + "tests/marketplace-$suite-test.php")
    }
    Invoke-ResilienceCheck 'native leases and loaded-page exclusion' 'python' @('tests/marketplace-live-agent-test.py')
    # This test hashes the supplied source saves, copies them to temporary player
    # profiles, and verifies the supplied files remain byte-for-byte unchanged.
    Invoke-ResilienceCheck 'HTTP/save interruption matrix' 'python' @('tests/marketplace-agent-test.py',$phpPath,$helperPath,(Resolve-Path $ModData).Path,(Resolve-Path $ReferenceSaves).Path)
    $report = Join-Path $repo 'build-marketplace/resilience-last.json'
    [ordered]@{ completed=(Get-Date).ToUniversalTime().ToString('o'); checks=$results } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $report -Encoding utf8
    Write-Output "All resilience checks passed. Report: $report"
} finally { Pop-Location }
