[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$base = Join-Path $env:USERPROFILE '.d2r-marketplace'
$configPath = Join-Path $base 'custody-enrollment.json'
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$root = $config.state_directory
$lease = [IO.File]::Open((Join-Path $root 'worker.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try {
    $recordPath = Join-Path $root 'worker.json'
    if (Test-Path -LiteralPath $recordPath) {
        $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
        $existing = Get-Process -Id $record.pid -ErrorAction SilentlyContinue
        if ($existing) {
            $details = Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)"
            if ($existing.StartTime.ToUniversalTime().Ticks -eq [long]$record.started_ticks -and $details.CommandLine -like "*$($config.agent_script)*" -and $details.CommandLine -like '*--watch*') {
                Write-Output 'Custody agent is already running.'
                return
            }
            Write-Verbose 'The old custody PID is stale. Its unrelated process will be left alone.'
        }
    }
    $arguments = @(('"' + $config.agent_script + '"'), ('"' + $configPath + '"'), '--watch')
    $process = Start-Process -FilePath $config.python -ArgumentList $arguments -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root 'worker.log') -RedirectStandardError (Join-Path $root 'worker-error.log') -PassThru
    @{ pid=$process.Id; started_ticks=$process.StartTime.ToUniversalTime().Ticks.ToString(); script=$config.agent_script } | ConvertTo-Json | Set-Content -LiteralPath $recordPath -Encoding UTF8
    Write-Output 'Custody agent started; close and reopen the stash to sync trades.'
} finally { $lease.Dispose() }
