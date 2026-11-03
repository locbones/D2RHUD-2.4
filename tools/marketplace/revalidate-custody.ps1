[CmdletBinding()]
param(
    # Supply the build artifact whose launch-guard integration has been verified.
    [Parameter(Mandatory)][string]$VerifiedHudPath,
    [string]$VerifiedLauncherPath,
    [string]$EnrollmentPath = (Join-Path $env:USERPROFILE '.d2r-marketplace\custody-enrollment.json')
)
$ErrorActionPreference = 'Stop'
function Get-CustodyHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose(); $stream.Dispose() }
}
if (Get-Process D2R,D2R_Debug,D2R_Debug_NoErrors -ErrorAction SilentlyContinue) {
    throw 'Close the game before revalidating its HUD.'
}
if ($VerifiedLauncherPath -and (Get-Process Pandemonium -ErrorAction SilentlyContinue)) {
    throw 'Close Pandemonium before revalidating its launcher.'
}
$EnrollmentPath = (Resolve-Path -LiteralPath $EnrollmentPath).Path
$originalHash = (Get-CustodyHash $EnrollmentPath)
$config = Get-Content -LiteralPath $EnrollmentPath -Raw | ConvertFrom-Json
if ($config.schema -ne 1 -or $config.enrolled -ne $true -or $config.simulation -eq $true) {
    throw 'An existing live custody enrollment is required.'
}
$verifiedHud = (Resolve-Path -LiteralPath $VerifiedHudPath).Path
$expectedHash = (Get-CustodyHash $verifiedHud)
$verifiedLauncher = if ($VerifiedLauncherPath) { (Resolve-Path -LiteralPath $VerifiedLauncherPath).Path } else { $null }
$expectedLauncherHash = if ($verifiedLauncher) { Get-CustodyHash $verifiedLauncher } else { $config.launcher_sha256 }
$candidate = $EnrollmentPath + '.revalidate-' + [guid]::NewGuid().ToString('N')
$handles = [Collections.Generic.List[IDisposable]]::new()
try {
    # The same exclusive lease used by the save writer excludes a running game,
    # a launcher handover, and any companion transaction during the pin update.
    $guardPath = Join-Path $config.state_directory 'guard.lock'
    $handles.Add([IO.File]::Open($guardPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None))
    if (Test-Path -LiteralPath (Join-Path $config.state_directory 'pending.json')) {
        throw 'Recover the pending custody transaction before revalidating the HUD.'
    }
    $installedHud = Join-Path $config.game_directory 'd2rhud.dll'
    foreach ($path in @($verifiedHud,$installedHud,$config.launcher_path,$config.helper)) {
        $handles.Add([IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
    }
    if ($verifiedLauncher) {
        $handles.Add([IO.File]::Open($verifiedLauncher,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read))
        if ((Get-CustodyHash $config.launcher_path) -ne $expectedLauncherHash -or
            (Get-CustodyHash $verifiedLauncher) -ne $expectedLauncherHash) {
            throw 'The installed launcher does not match the verified build artifact. No pins were changed.'
        }
    }
    if ((Get-CustodyHash $installedHud) -ne $expectedHash -or
        (Get-CustodyHash $verifiedHud) -ne $expectedHash) {
        throw 'The installed HUD does not match the verified build artifact. No pins were changed.'
    }
    $oldHash = $config.hud_sha256
    $oldLauncherHash = $config.launcher_sha256
    $config.hud_sha256 = $expectedHash
    $config.launcher_sha256 = $expectedLauncherHash
    [IO.File]::WriteAllText($candidate,($config | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
    # Constructing the installed agent performs its real helper, compiled-catalog,
    # launcher and HUD checks. Do not run it: revalidation never writes a save or
    # starts a cloud transfer. Launcher updates require their own verified artifact;
    # helper and catalog changes still require enrollment.
    $probe = @'
import runpy,sys
a=runpy.run_path(sys.argv[1])
a['Agent'](a['io'].document(sys.argv[2]))
print('Installed agent validation passed.')
'@
    & $config.python -c $probe $config.agent_script $candidate
    if ($LASTEXITCODE -ne 0) { throw 'Custody validation failed. No pins were changed.' }
    if ((Get-CustodyHash $EnrollmentPath) -ne $originalHash) {
        throw 'Enrollment changed during validation. No pins were changed.'
    }
    if ($oldHash -eq $expectedHash -and $oldLauncherHash -eq $expectedLauncherHash) {
        Write-Output 'The verified HUD is already enrolled; launcher, helper and catalog checks passed.'
        return
    }
    $backup = Join-Path $config.state_directory ('backups\hud-revalidation-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    [IO.File]::Replace($candidate,$EnrollmentPath,(Join-Path $backup 'custody-enrollment.json'))
    @{
        at=[DateTime]::UtcNow.ToString('o'); verified_hud=$verifiedHud
        previous_hud_sha256=$oldHash; hud_sha256=$expectedHash
        previous_launcher_sha256=$oldLauncherHash; verified_launcher=$verifiedLauncher
        launcher_sha256=$config.launcher_sha256; helper_sha256=$config.helper_sha256; catalog=$config.catalog
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'validation.json') -Encoding UTF8
    Write-Output "HUD custody pin revalidated. No saves changed. Previous enrollment: $backup"
} finally {
    if (Test-Path -LiteralPath $candidate) { Remove-Item -LiteralPath $candidate }
    for ($index=$handles.Count-1; $index -ge 0; $index--) { $handles[$index].Dispose() }
}
