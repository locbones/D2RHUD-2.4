param(
    [Parameter(Mandatory)][int]$GameProcessId,
    [double]$Seconds = 120,
    [ValidateRange(10,1000)][int]$SampleMilliseconds = 20,
    [ValidateRange(0,1000)][int]$ProfileMilliseconds = 0,
    [string]$StopFile = '',
    [string]$Output = (Join-Path $env:TEMP ('game-transition-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json'))
)
$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'GameTransitionBenchmark/GameTransitionBenchmark.csproj'
$runArguments = @('--pid', $GameProcessId.ToString(), '--seconds', $Seconds.ToString([cultureinfo]::InvariantCulture), '--sample-ms', $SampleMilliseconds.ToString(), '--profile-ms', $ProfileMilliseconds.ToString(), '--output', $Output)
if ($StopFile) { $runArguments += @('--stop-file', $StopFile) }
& dotnet run --project $project --configuration Release -- @runArguments
if ($LASTEXITCODE -ne 0) { throw "Transition observer failed (exit $LASTEXITCODE). Report: $Output" }
