[CmdletBinding()]
param([switch]$Hardcore)
$ErrorActionPreference = 'Stop'
$directory = Join-Path $env:USERPROFILE '.d2r-marketplace'
$mode = if ($Hardcore) { 'HC' } else { 'SC' }
$recordPath = Join-Path $directory "sync-$mode.json"
if (-not (Test-Path -LiteralPath $recordPath)) { Write-Output "Marketplace $mode sync is not running."; return }
$lease = [IO.File]::Open((Join-Path $directory "sync-$mode.lock"), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try {
    $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    $process = Get-Process -Id $record.pid -ErrorAction SilentlyContinue
    if ($process) {
        $entrypoint = Join-Path $PSScriptRoot 'sync-dreamhost.ps1'
        $details = Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)"
        $startedTicks = if ($record.started_ticks) { [long]$record.started_ticks } else { ([datetime]$record.started).ToUniversalTime().Ticks }
        if ($process.StartTime.ToUniversalTime().Ticks -ne $startedTicks -or $details.Name -ne 'powershell.exe' -or $details.CommandLine -notlike "*$entrypoint*" -or $details.CommandLine -notlike '*-Watch*') { throw 'The saved PID is not this sync worker. Nothing was stopped.' }
        $client = Join-Path $PSScriptRoot 'sync-marketplace.py'
        Get-CimInstance Win32_Process -Filter "ParentProcessId=$($record.pid)" | Where-Object { $_.Name -match '^python(?:[0-9.]*)?\.exe$' -and $_.CommandLine -like "*$client*" -and $_.CommandLine -like '*--watch*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $recordPath
    Write-Output "Marketplace $mode sync stopped."
} finally { $lease.Dispose() }
