# Run this yourself in PowerShell so SSH reads your password privately.
# This authorizes the existing local public key; no private key is uploaded.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
if ($Host.Name -eq 'Windows PowerShell ISE Host') {
    # ISE has no interactive console for native SSH trust/password prompts.
    # Open a real console for the user, preserving its output after completion.
    $consoleExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    Write-Host 'Opening a normal PowerShell console for SSH. Enter your password in that new window.'
    Start-Process -FilePath $consoleExe -ArgumentList @('-NoLogo', '-NoProfile', '-NoExit', '-File', ('"' + $PSCommandPath + '"')) -WindowStyle Normal | Out-Null
    return
}
$serverName = 'pdx1-shared-a1-20.dreamhost.com'
$serverUser = 'bonesy'
$publicKeyPath = Join-Path $env:USERPROFILE '.ssh\id_ed25519.pub'
if (-not (Test-Path -LiteralPath $publicKeyPath)) {
    throw "The existing public key is missing: $publicKeyPath"
}
$publicKey = (Get-Content -LiteralPath $publicKeyPath -Raw).Trim()
if ($publicKey -notmatch '^ssh-ed25519 ([A-Za-z0-9+/]+={0,2})( [^\r\n]*)?$') {
    throw 'Expected exactly one ED25519 public key.'
}
# Use only the validated key type/data in the remote command, omitting comments.
$keyEntry = 'restrict ssh-ed25519 ' + $Matches[1]
Write-Host 'First, verify this server in DreamHost: Servers & Usage > Manage > SSH Keys > SSH Fingerprints.'
Write-Host 'The ED25519 fingerprint observed during the connection check was:'
Write-Host 'SHA256:sIyfKH7700uFlH7Znq1fCXeoe/4UP2mtHjhURDA7DP4'
Write-Host 'Compare against the panel before accepting the SSH prompt. If they differ, stop.'
Write-Host 'If SSH asks yes/no/[fingerprint], that is the server-trust prompt, not the password prompt.'
Write-Host 'Enter the password for the website user bonesy when SSH asks. It will not display as you type.'
# Keep native command arguments free of double quotes for Windows PowerShell
# 5.1 compatibility. SSH starts in the remote home; cd also makes that explicit.
# Do not pipe into SSH or run a remote command that waits for stdin/EOF. The
# public key is non-secret and its validated alphabet is safe in single quotes.
# Add a leading newline so an existing entry without its final newline survives.
$remoteSetup = "set -eu; cd; umask 077; mkdir -p .ssh; chmod 700 .ssh; touch .ssh/authorized_keys; if ! grep -qxF '$keyEntry' .ssh/authorized_keys; then printf '\n%s\n' '$keyEntry' >> .ssh/authorized_keys; fi; chmod 600 .ssh/authorized_keys; echo Marketplace_deployment_key_authorized"
Write-Host 'Connecting. A successful login will print Marketplace_deployment_key_authorized.'
& ssh -T -o StrictHostKeyChecking=ask -o NumberOfPasswordPrompts=2 -o ConnectTimeout=15 -o ServerAliveInterval=10 -o ServerAliveCountMax=2 -p 22 "$serverUser@$serverName" $remoteSetup
if ($LASTEXITCODE -ne 0) {
    throw 'SSH setup did not complete. No marketplace files were uploaded.'
}
Write-Host 'Connection setup complete. Tell Codex to continue the marketplace deployment.'
