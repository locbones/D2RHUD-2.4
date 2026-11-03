param(
    [string]$Label = 'baseline',
    [string]$Output = (Join-Path $env:TEMP ('game-startup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')),
    [double]$ObserveSeconds = 8,
    [double]$TimeoutSeconds = 45,
    [string]$Settings = '',
    [ValidateRange(0,1000)][int]$ProfileMilliseconds = 0,
    [bool]$Frontend = $true,
    [switch]$DisableProfiler,
    [switch]$KeepOpenLab,
    [ValidateSet('Current', 'On', 'Off')][string]$CompileText = 'Current'
)
$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'GameStartupBenchmark/GameStartupBenchmark.csproj'
$runArguments = @('--label', $Label, '--output', $Output, '--observe-seconds', $ObserveSeconds.ToString([cultureinfo]::InvariantCulture), '--timeout-seconds', $TimeoutSeconds.ToString([cultureinfo]::InvariantCulture))
$runArguments += @('--frontend', $Frontend.ToString())
if ($Settings) { $runArguments += @('--settings', $Settings) }
if ($ProfileMilliseconds) { $runArguments += @('--profile-ms', $ProfileMilliseconds) }
if ($DisableProfiler) { $runArguments += @('--disable-profiler', 'true') }
if ($KeepOpenLab) { $runArguments += @('--keep-open-lab', 'true') }
if ($CompileText -ne 'Current') { $runArguments += @('--compile-text', ($CompileText -eq 'On').ToString()) }
& dotnet run --project $project --configuration Release -- @runArguments
if ($LASTEXITCODE -ne 0) { throw "Startup benchmark failed (exit $LASTEXITCODE). Report: $Output" }
