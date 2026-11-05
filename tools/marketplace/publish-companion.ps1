# Developer release preparation. Never bundle the private signing key.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$Payload=(Join-Path $PSScriptRoot '../../../D2RLAN-CrossPlatform/CrossPlatform/src/Pandemonium.Core/Resources/Marketplace/companion-win-x64.zip'),
    [string]$SigningKey=(Join-Path $env:USERPROFILE '.d2r-marketplace-signing/companion-rsa.pem')
)
$ErrorActionPreference='Stop'
$payloadPath=[IO.Path]::GetFullPath($Payload)
$outputPath=[IO.Path]::GetFullPath($OutputDirectory)
$archive=[IO.Compression.ZipFile]::OpenRead($payloadPath)
try{$entry=$archive.GetEntry('manifest.json');if(-not $entry){throw 'Runtime manifest missing'};$reader=[IO.StreamReader]::new($entry.Open());try{$runtime=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}}finally{$archive.Dispose()}
if($runtime.version -notmatch '^\d+\.\d+\.\d+$' -or -not $runtime.files.'companion-update.py'){throw 'Build a versioned runtime with the updater before publishing'}
$bytes=[IO.File]::ReadAllBytes($payloadPath)
if($bytes.Length -gt 40MB){throw 'Companion exceeds the update size limit'}
$sha=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
$manifest=[ordered]@{schema=1;platform='win-x64';protocol=1;version=$runtime.version;sequence=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();size=$bytes.Length;sha256=$sha;file=($sha+'.zip')}
$raw=[Text.Encoding]::UTF8.GetBytes(($manifest|ConvertTo-Json -Compress))
$rsa=[Security.Cryptography.RSA]::Create()
try{$rsa.ImportFromPem([IO.File]::ReadAllText($SigningKey));$signature=$rsa.SignData($raw,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)}finally{$rsa.Dispose()}
New-Item -ItemType Directory -Path $outputPath -Force|Out-Null
[IO.File]::WriteAllBytes((Join-Path $outputPath $manifest.file),$bytes)
$envelope=@{payload=[Convert]::ToBase64String($raw);signature=[Convert]::ToBase64String($signature)}|ConvertTo-Json -Compress
[IO.File]::WriteAllText((Join-Path $outputPath 'manifest.json'),$envelope,[Text.UTF8Encoding]::new($false))
$manifest|ConvertTo-Json
