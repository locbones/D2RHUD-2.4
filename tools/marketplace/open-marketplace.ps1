[CmdletBinding()]
param([switch]$NoBrowser)
$ErrorActionPreference = 'Stop'
$root = Join-Path $env:USERPROFILE '.d2r-marketplace'
$config = Get-Content -LiteralPath (Join-Path $root 'sync-config.json') -Raw | ConvertFrom-Json
$pairing = Get-Content -LiteralPath (Join-Path $root 'pairing.json') -Raw | ConvertFrom-Json
$endpoint = [uri]$config.endpoint
if ($endpoint.Scheme -ne 'https' -or $endpoint.UserInfo -or $endpoint.Query -or $endpoint.Fragment) { throw 'Use the paired HTTPS marketplace endpoint.' }
$ticket = Invoke-RestMethod -Uri ($endpoint.AbsoluteUri + '?action=browser-ticket') -Method Post -Headers @{Authorization=('Bearer ' + $pairing.token)} -MaximumRedirection 0
# A short-lived, single-use code goes in the fragment; the server access log and
# referrer never see it. The page removes it before loading authenticated data.
$url = [uri]::new($endpoint, './').AbsoluteUri + '#connect=' + $ticket.code
if ($NoBrowser) { return $url }
Start-Process -FilePath $url
