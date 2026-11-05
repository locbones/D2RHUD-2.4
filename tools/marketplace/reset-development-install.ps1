# Operator-only clean-install rehearsal. Never shipped as a player setup step.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$BackupDirectory, [switch]$Apply, [string]$TestRoot)
$ErrorActionPreference = 'Stop'
$fixtureMode = -not [string]::IsNullOrWhiteSpace($TestRoot)
if ($fixtureMode) {
    $fixtureRoot = [IO.Path]::GetFullPath($TestRoot)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($env:D2_MARKET_RESET_TEST -ne '1' -or -not $fixtureRoot.StartsWith($temporaryRoot,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath (Join-Path $fixtureRoot 'marketplace-reset-fixture.json'))) { throw 'Reset fixtures must be explicitly marked inside the temporary directory.' }
    $marketRoot = Join-Path $fixtureRoot 'profile'
    $backupParent = Join-Path $fixtureRoot 'backups'
} else {
    $marketRoot = [IO.Path]::GetFullPath((Join-Path $env:USERPROFILE '.d2r-marketplace'))
    $backupParent = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Pandemonium\Backups\Marketplace'))
    if (Get-Process D2R,D2R_Debug,D2R_Debug_NoErrors,Pandemonium -ErrorAction SilentlyContinue) { throw 'Close D2R and Pandemonium before the clean-install reset.' }
}
$backupRoot = [IO.Path]::GetFullPath($BackupDirectory)
if (-not $backupRoot.StartsWith($backupParent.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $backupRoot)) { throw 'Choose a new backup directory inside the managed Marketplace backup folder.' }
if (-not (Test-Path -LiteralPath $marketRoot -PathType Container)) { throw 'There is no installed marketplace profile to reset.' }
$rootItem = Get-Item -LiteralPath $marketRoot -Force
if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or (Get-ChildItem -LiteralPath $marketRoot -Force -Recurse | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })) { throw 'Refusing to reset a marketplace profile containing links.' }
$profiles = @(Get-ChildItem -LiteralPath $marketRoot -Recurse -File -Filter 'custody-enrollment.json' | Where-Object { $_.FullName -eq (Join-Path $marketRoot 'custody-enrollment.json') -or $_.Directory.Parent.Name -eq 'profiles' })
$enrollments = @($profiles | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json })
$sidecars = @()
foreach ($folder in @($enrollments.save_directory | Sort-Object -Unique)) {
    if (-not [IO.Path]::IsPathFullyQualified($folder) -or -not (Test-Path -LiteralPath $folder -PathType Container)) { throw 'An enrolled save folder is missing or invalid.' }
    $sidecars += @(Get-ChildItem -LiteralPath $folder -File -Force | Where-Object { $_.Name -match '^Marketplace[^\\/]*\.json$|^\.market-link-[a-f0-9]{32}\.tmp$' })
}
if (-not $Apply) {
    [pscustomobject]@{ profile=$marketRoot; backup=$backupRoot; marketplace_sidecars=$sidecars.Count; native_saves_removed=0; ready=$true } | ConvertTo-Json
    return
}
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
if (-not $fixtureMode) {
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    & icacls.exe $backupRoot /inheritance:r /grant:r "*$($sid):(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot protect the backup credentials.' }
}
$startupTask = $null
$startupRun = $null
$leases = @()
$completed = $false
$startupChanged = $false
try {
    if (-not $fixtureMode) {
        $startupTask = Get-ScheduledTask -TaskName 'D2R Marketplace' -ErrorAction SilentlyContinue
        if ($startupTask) {
            if (-not (@($startupTask.Actions | Where-Object { $_.Arguments -and $_.Arguments.Contains($marketRoot,[StringComparison]::OrdinalIgnoreCase) }).Count)) { throw 'The startup task is not the installed marketplace task.' }
            Export-ScheduledTask -TaskName 'D2R Marketplace' | Set-Content -LiteralPath (Join-Path $backupRoot 'startup-task.xml') -Encoding UTF8
        }
        $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
        $startupRun = (Get-ItemProperty -LiteralPath $runKey -Name 'Pandemonium Marketplace' -ErrorAction SilentlyContinue).'Pandemonium Marketplace'
        if ($startupRun -and -not $startupRun.Contains($marketRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'The startup entry belongs to another installation.' }
    }
    @{ task_enabled=($startupTask -and $startupTask.State -ne 'Disabled'); run=$startupRun; profile=$marketRoot } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backupRoot 'startup.json') -Encoding UTF8
    foreach ($state in @($enrollments.state_directory | Sort-Object -Unique)) {
        $statePath = [IO.Path]::GetFullPath($state)
        if (-not $statePath.StartsWith($marketRoot.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'A recovery directory is outside the marketplace profile.' }
        $lease = $null
        for ($attempt=0; $attempt -lt 100 -and -not $lease; $attempt++) {
            try { $lease=[IO.File]::Open((Join-Path $statePath 'guard.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
            catch [IO.IOException] { Start-Sleep -Milliseconds 100 }
        }
        if (-not $lease) { throw 'A save operation is busy; nothing has been removed. Retry after it finishes.' }
        $leases += $lease
        if (Test-Path -LiteralPath (Join-Path $statePath 'pending.json')) { throw 'Recover the pending transfer before resetting. Its journal has been preserved.' }
    }
    if (-not $fixtureMode) {
        $startupChanged = $true
        if ($startupTask) { Disable-ScheduledTask -TaskName 'D2R Marketplace' | Out-Null; Stop-ScheduledTask -TaskName 'D2R Marketplace' -ErrorAction SilentlyContinue }
        if ($startupRun) { Remove-ItemProperty -LiteralPath $runKey -Name 'Pandemonium Marketplace' }
        $owned = @{}
        $records = Get-ChildItem -LiteralPath $marketRoot -Recurse -File | Where-Object { $_.Name -match '^(sync-(SC|HC)|worker|managed-host)\.json$' }
        foreach ($file in $records) {
            $record = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
            $script = if ($record.script) { $record.script } elseif ($record.entrypoint) { $record.entrypoint } elseif ($record.runtime) { Join-Path $record.runtime 'companion-host.py' } else { $null }
            if (-not $script -or -not $script.StartsWith($marketRoot,[StringComparison]::OrdinalIgnoreCase)) { continue }
            $process = Get-Process -Id $record.pid -ErrorAction SilentlyContinue
            if (-not $process -or $process.StartTime.ToUniversalTime().Ticks -ne [long]$record.started_ticks) { continue }
            $details = Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)"
            if ($details.CommandLine -and $details.CommandLine.Contains($script,[StringComparison]::OrdinalIgnoreCase)) { $owned[$process.Id]=$process }
        }
        # Older sync records point to a PowerShell parent. Include only its
        # verified Python descendants whose command also names this profile.
        $all = @(Get-CimInstance Win32_Process)
        do {
            $added = $false
            foreach ($details in $all) {
                if (-not $owned.ContainsKey([int]$details.ProcessId) -and $owned.ContainsKey([int]$details.ParentProcessId) -and $details.Name -match '^python(w)?\.exe$' -and $details.CommandLine -and $details.CommandLine.Contains($marketRoot,[StringComparison]::OrdinalIgnoreCase)) {
                    $child = Get-Process -Id $details.ProcessId -ErrorAction SilentlyContinue
                    if ($child) { $owned[$child.Id]=$child; $added=$true }
                }
            }
        } while ($added)
        foreach ($process in @($owned.Values | Sort-Object @{Expression={ if ($_.ProcessName -match 'pythonw|powershell') { 0 } else { 1 } }})) {
            $current = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
            if ($current -and $current.StartTime.ToUniversalTime().Ticks -eq $process.StartTime.ToUniversalTime().Ticks) { Stop-Process -Id $current.Id; $current.WaitForExit(5000) | Out-Null }
        }
    }
    foreach ($lease in $leases) { $lease.Dispose() }; $leases=@()
    Copy-Item -LiteralPath $marketRoot -Destination (Join-Path $backupRoot 'profile') -Recurse -Force
    $manifest = @()
    foreach ($file in Get-ChildItem -LiteralPath $marketRoot -Recurse -Force -File) {
        $relative = $file.FullName.Substring($marketRoot.Length).TrimStart('\','/')
        $target = Join-Path (Join-Path $backupRoot 'profile') $relative
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if ($hash -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) { throw 'Marketplace backup verification failed. Active files are preserved.' }
        $manifest += @{ source=$file.FullName; backup=$target; sha256=$hash; remove=$true }
    }
    $profileFileCount = $manifest.Count
    $index = 0
    foreach ($file in $sidecars) {
        $target = Join-Path $backupRoot ('sidecars\' + $index + '\' + $file.Name); $index++
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if ($hash -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) { throw 'Sidecar backup verification failed. Active files are preserved.' }
        $manifest += @{ source=$file.FullName; backup=$target; sha256=$hash; remove=$true }
    }
    $index = 0
    foreach ($config in $enrollments) {
        foreach ($file in @($config.launcher_path,(Join-Path $config.game_directory 'd2rhud.dll'))) {
            if ($file -and (Test-Path -LiteralPath $file -PathType Leaf)) {
                $target = Join-Path $backupRoot ('binaries\' + $index + '\' + [IO.Path]::GetFileName($file)); $index++
                New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
                Copy-Item -LiteralPath $file -Destination $target
                $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
                if ($hash -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) { throw 'Binary backup verification failed.' }
                $manifest += @{ source=$file; backup=$target; sha256=$hash; remove=$false }
            }
        }
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backupRoot 'manifest.json') -Encoding UTF8
    # Recheck every source immediately before deletion. No character/stash save
    # is a target, and all recovery data already has a verified private copy.
    foreach ($entry in $manifest) { if ((Get-FileHash -LiteralPath $entry.source -Algorithm SHA256).Hash -ne $entry.sha256) { throw 'A setup file changed during backup; reset stopped.' } }
    if (-not $fixtureMode -and (Get-Process D2R,D2R_Debug,D2R_Debug_NoErrors,Pandemonium -ErrorAction SilentlyContinue)) { throw 'The game or launcher reopened. Active files are preserved.' }
    if ([IO.Path]::GetFullPath($marketRoot) -ne $rootItem.FullName -or -not [IO.Path]::IsPathFullyQualified($marketRoot)) { throw 'The resolved reset target changed.' }
    if (@(Get-ChildItem -LiteralPath $marketRoot -Recurse -Force -File).Count -ne $profileFileCount) { throw 'The active profile changed during backup; reset stopped.' }
    foreach ($file in $sidecars) { Remove-Item -LiteralPath $file.FullName -Force }
    Remove-Item -LiteralPath $marketRoot -Recurse -Force
    $completed = $true
    @{ backup=$backupRoot; verified_files=$manifest.Count; removed_profile=$marketRoot; removed_sidecars=$sidecars.Count; native_saves_removed=0; old_online_account='preserved' } | ConvertTo-Json | Tee-Object -FilePath (Join-Path $backupRoot 'reset-result.json')
} finally {
    foreach ($lease in $leases) { $lease.Dispose() }
    if (-not $completed -and $startupChanged) {
        if ($startupTask -and $startupTask.State -ne 'Disabled') { Enable-ScheduledTask -TaskName 'D2R Marketplace' | Out-Null }
        if ($startupRun) { Set-ItemProperty -LiteralPath $runKey -Name 'Pandemonium Marketplace' -Value $startupRun }
    }
}
