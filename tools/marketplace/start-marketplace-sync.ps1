[CmdletBinding()]
param([switch]$Hardcore,[string]$SaveFolder)
$ErrorActionPreference = 'Stop'
$directory = Join-Path $env:USERPROFILE '.d2r-marketplace'
if (-not $SaveFolder) {
    $enrollment = Join-Path $directory 'custody-enrollment.json'
    $SaveFolder = if (Test-Path -LiteralPath $enrollment) { (Get-Content -LiteralPath $enrollment -Raw | ConvertFrom-Json).save_directory } else { Join-Path $env:USERPROFILE 'Saved Games\Diablo II Resurrected\mods\RMD-MP' }
}
if (-not (Test-Path -LiteralPath (Join-Path $directory 'pairing.json'))) { throw 'Pair this computer before starting sync.' }
$mode = if ($Hardcore) { 'HC' } else { 'SC' }
$recordPath = Join-Path $directory "sync-$mode.json"
$entrypoint = Join-Path $PSScriptRoot 'sync-dreamhost.ps1'
$lease = [IO.File]::Open((Join-Path $directory "sync-$mode.lock"), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try {
    if (Test-Path -LiteralPath $recordPath) {
        $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
        $existing = Get-Process -Id $record.pid -ErrorAction SilentlyContinue
        if ($existing) {
            $details = Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)"
            $startedTicks = if ($record.started_ticks) { [long]$record.started_ticks } else { ([datetime]$record.started).ToUniversalTime().Ticks }
            if ($record.entrypoint -and $existing.StartTime.ToUniversalTime().Ticks -eq $startedTicks -and $details.CommandLine -like "*$($record.entrypoint)*" -and $details.CommandLine -like '*-Watch*') {
                Write-Output "Marketplace $mode sync is already running (PID $($record.pid))."
                return
            }
            Write-Verbose 'The old sync PID is stale. Its unrelated process will be left alone.'
        }
    }
    $console = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $arguments = @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', ('"' + $entrypoint + '"'), '-Watch')
    $arguments += @('-SaveFolder', ('"' + $SaveFolder + '"'))
    if ($Hardcore) { $arguments += '-Hardcore' }
    $process = Start-Process -FilePath $console -ArgumentList $arguments -WindowStyle Hidden -RedirectStandardOutput (Join-Path $directory "sync-$mode.log") -RedirectStandardError (Join-Path $directory "sync-$mode-error.log") -PassThru
    $record = @{ pid=$process.Id; started_ticks=$process.StartTime.ToUniversalTime().Ticks.ToString(); entrypoint=$entrypoint; mode=$mode }
    $record | ConvertTo-Json | Set-Content -LiteralPath $recordPath -Encoding UTF8
    Write-Output "Marketplace $mode sync started (PID $($process.Id)); local changes sync immediately."
} finally { $lease.Dispose() }
