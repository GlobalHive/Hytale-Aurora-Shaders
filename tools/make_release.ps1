# Builds the release zip: every tracked file plus a freshly built bin\opengl32.dll.
#   tools\make_release.ps1 -Version 1.0.0     ->  dist\Aurora-Shaders-1.0.0.zip
param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $root 'build.ps1')

$dist = Join-Path $root 'dist'
$stage = Join-Path $dist "Aurora-Shaders-$Version"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Push-Location $root
try {
    foreach ($f in (git ls-files)) {
        $dst = Join-Path $stage $f
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item -LiteralPath $f -Destination $dst
    }
} finally { Pop-Location }
New-Item -ItemType Directory -Force (Join-Path $stage 'bin') | Out-Null
Copy-Item (Join-Path $root 'build\opengl32.dll') (Join-Path $stage 'bin\opengl32.dll')

$zip = Join-Path $dist "Aurora-Shaders-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Release: $zip"
