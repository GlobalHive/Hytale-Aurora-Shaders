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
    foreach ($f in (git ls-files --cached --others --exclude-standard)) {
        if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { continue }
        $dst = Join-Path $stage $f
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item -LiteralPath $f -Destination $dst
    }
} finally { Pop-Location }
New-Item -ItemType Directory -Force (Join-Path $stage 'bin') | Out-Null
Copy-Item (Join-Path $root 'build\opengl32.dll') (Join-Path $stage 'bin\opengl32.dll')
# NVIDIA's DLSS runtime (not in git; redistributable under the DLSS SDK license, shipped with it).
$dlss = Join-Path $root 'shaderpack\dlss'
if (Test-Path (Join-Path $dlss 'nvngx_dlss.dll')) {
    New-Item -ItemType Directory -Force (Join-Path $stage 'shaderpack\dlss') | Out-Null
    Copy-Item (Join-Path $dlss 'nvngx_dlss*.dll') (Join-Path $stage 'shaderpack\dlss')
    $lic = Join-Path $root 'third_party\DLSS\LICENSE.txt'
    if (Test-Path $lic) { Copy-Item $lic (Join-Path $stage 'shaderpack\dlss\NVIDIA-DLSS-LICENSE.txt') }
} else {
    Write-Host 'No DLSS runtime in shaderpack\dlss: the release ships without DLSS'
}

$streamline = Join-Path $root 'shaderpack\streamline'
if (Test-Path (Join-Path $streamline 'sl.interposer.dll')) {
    $destination = Join-Path $stage 'shaderpack\streamline'
    New-Item -ItemType Directory -Force $destination | Out-Null
    foreach ($name in @('sl.interposer.dll', 'sl.common.dll', 'sl.reflex.dll', 'sl.pcl.dll', 'sl.dlss_g.dll', 'nvngx_dlssg.dll')) {
        $source = Join-Path $streamline $name
        $signature = Get-AuthenticodeSignature -LiteralPath $source
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=NVIDIA Corporation') {
            throw "Invalid Frame Generation runtime signature: $name"
        }
        Copy-Item -LiteralPath $source -Destination $destination
    }
    foreach ($name in @('nvngx_dlss.license.txt', 'reflex.license.txt', 'Streamline-license.txt', 'Streamline-3rd-party-licenses.md')) {
        Copy-Item -LiteralPath (Join-Path $streamline $name) -Destination $destination
    }
}

$zip = Join-Path $dist "Aurora-Shaders-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip }
# Windows' tar.exe writes standard zips; Compress-Archive and .NET's ZipFile on Windows
# PowerShell write backslash paths that other unzip tools mangle.
& (Join-Path $env:SystemRoot 'System32\tar.exe') -a -cf $zip -C $dist "Aurora-Shaders-$Version"
if ($LASTEXITCODE -ne 0) { throw "tar failed ($LASTEXITCODE)" }
Write-Host "Release: $zip"
