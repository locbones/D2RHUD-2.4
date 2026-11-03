[CmdletBinding()]
param([switch]$InstallStartup)
$ErrorActionPreference = 'Stop'
$enrollment = Get-Content -LiteralPath (Join-Path $env:USERPROFILE '.d2r-marketplace\custody-enrollment.json') -Raw | ConvertFrom-Json
& (Join-Path $PSScriptRoot 'start-marketplace-sync.ps1') -SaveFolder $enrollment.save_directory
if (Test-Path -LiteralPath (Join-Path $enrollment.save_directory 'Marketplace_2.4_HC.json')) {
    & (Join-Path $PSScriptRoot 'start-marketplace-sync.ps1') -Hardcore -SaveFolder $enrollment.save_directory
}
& (Join-Path $PSScriptRoot 'start-custody-agent.ps1')
if ($InstallStartup) {
    # Task Scheduler starts a console executable before its -WindowStyle option
    # is parsed. Use Python's GUI executable so even that initial flash is absent.
    $windowless = Join-Path (Split-Path -Parent $enrollment.python) 'pythonw.exe'
    if (-not (Test-Path -LiteralPath $windowless -PathType Leaf)) { throw 'Python pythonw.exe is required for windowless marketplace startup.' }
    $arguments = '"' + (Join-Path $PSScriptRoot 'background-marketplace.py') + '"'
    $action = New-ScheduledTaskAction -Execute $windowless -Argument $arguments -WorkingDirectory $PSScriptRoot
    $user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    $logon = New-ScheduledTaskTrigger -AtLogOn -User $user
    $daily = New-ScheduledTaskTrigger -Daily -At '00:00'
    $daily.Repetition = (New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Minutes 5) -RepetitionDuration (New-TimeSpan -Days 1)).Repetition
    $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
    $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 2)
    Register-ScheduledTask -TaskName 'D2R Marketplace' -Action $action -Trigger @($logon,$daily) -Principal $principal -Settings $settings -Description 'Start or recover the player marketplace companions. Game save writes wait for the native game lease.' -Force | Out-Null
    Write-Output 'Marketplace companions start at Windows sign-in; a five-minute check restarts a missing worker.'
}
