# Exercise the real helper's remote command in a temporary directory, without
# connecting to DreamHost or reading any private key/password.
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $env:TEMP ('marketplace-ssh-helper-' + [guid]::NewGuid().ToString('N'))
$fixtureSSH = Join-Path $fixture '.ssh'
New-Item -ItemType Directory -Path $fixtureSSH | Out-Null
$authorized = Join-Path $fixtureSSH 'authorized_keys'
[IO.File]::WriteAllText($authorized, 'existing-entry-without-final-newline')
$fixtureUnix = '/' + $fixture.Substring(0,1).ToLowerInvariant() + $fixture.Substring(2).Replace('\','/')
$connectProbe = [pscustomobject]@{ calls = 0 }
function ssh {
    if ($MyInvocation.ExpectingInput) { throw 'SSH must inherit console input for its login prompt.' }
    $remote = [string]$args[-1]
    if ($remote -match '\btr\b|\bcat\b|\bread\b') { throw 'Remote bootstrap must not wait on stdin.' }
    if ($args -notcontains 'bonesy@pdx1-shared-a1-20.dreamhost.com') { throw 'Unexpected SSH destination.' }
    $sandboxed = $remote.Replace('set -eu; cd;', "set -eu; cd -- '$fixtureUnix';")
    & 'C:\Program Files\Git\bin\bash.exe' -c $sandboxed
    if ($LASTEXITCODE -ne 0) { throw 'Remote shell command failed.' }
    $connectProbe.calls++
    $global:LASTEXITCODE = 0
}
$helper = Join-Path $PSScriptRoot '..\tools\marketplace\connect-dreamhost.ps1'
& $helper
& $helper
$lines = [IO.File]::ReadAllLines($authorized)
if ($connectProbe.calls -ne 2 -or $lines[0] -ne 'existing-entry-without-final-newline') { throw 'Existing key entries changed.' }
if (@($lines | Where-Object { $_ -match '^restrict ssh-ed25519 ' }).Count -ne 1) { throw 'Repeated bootstrap duplicated or lost the deployment key.' }
Write-Output 'SSH helper: inherited login input, finite remote command, existing keys preserved, repeated authorization idempotent. No network connection made.'
