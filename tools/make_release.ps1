# Builds the release zip from committed source plus a freshly built bin\opengl32.dll.
#   tools\make_release.ps1 -Version 1.0.0     ->  dist\Aurora-Shaders-1.0.0.zip
param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$') { throw 'Version must be a semantic version.' }
Push-Location $root
try {
    git diff --quiet HEAD -- hook build.ps1
    if ($LASTEXITCODE -ne 0) { throw 'Commit hook/build changes before packaging so the DLL matches the release source.' }
} finally { Pop-Location }
& (Join-Path $root 'build.ps1')
& python (Join-Path $root 'tools\check_exports.py') (Join-Path $root 'build\opengl32.dll')
if ($LASTEXITCODE -ne 0) { throw 'Windows OpenGL export validation failed.' }

$dist = [IO.Path]::GetFullPath((Join-Path $root 'dist'))
$stage = [IO.Path]::GetFullPath((Join-Path $dist "Aurora-Shaders-$Version"))
if ([IO.Path]::GetDirectoryName($stage) -ine $dist) { throw 'Release staging path escaped dist.' }
if (Test-Path -LiteralPath $stage) {
    $existing = Get-Item -LiteralPath $stage -Force
    if ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Release staging folder must not be a link.' }
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force $stage | Out-Null
$snapshot = Join-Path $dist "Aurora-Shaders-$Version-source.tar"
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
Push-Location $root
try {
    git archive --format=tar "--output=$snapshot" HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Git source snapshot failed.' }
    & $tar -xf $snapshot -C $stage
    if ($LASTEXITCODE -ne 0) { throw 'Git source extraction failed.' }
} finally { Pop-Location }
Remove-Item -LiteralPath $snapshot
New-Item -ItemType Directory -Force (Join-Path $stage 'bin') | Out-Null
Copy-Item (Join-Path $root 'build\opengl32.dll') (Join-Path $stage 'bin\opengl32.dll')
# NVIDIA's DLSS runtime (not in git; redistributable under the DLSS SDK license, shipped with it).
$dlss = Join-Path $root 'shaderpack\dlss'
if (Test-Path (Join-Path $dlss 'nvngx_dlss.dll')) {
    New-Item -ItemType Directory -Force (Join-Path $stage 'shaderpack\dlss') | Out-Null
    foreach ($name in @('nvngx_dlss.dll', 'nvngx_dlssd.dll')) {
        $source = Join-Path $dlss $name
        $signature = Get-AuthenticodeSignature -LiteralPath $source
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=NVIDIA Corporation') {
            throw "Invalid NVIDIA DLSS runtime signature: $name"
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $stage 'shaderpack\dlss')
    }
    $lic = Join-Path $root 'third_party\DLSS\LICENSE.txt'
    Copy-Item -LiteralPath $lic -Destination (Join-Path $stage 'shaderpack\dlss\NVIDIA-DLSS-LICENSE.txt')
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
& python (Join-Path $root 'tools\package_release.py') $stage $zip
if ($LASTEXITCODE -ne 0) { throw 'Release archive validation failed.' }
Write-Host "Release: $zip"
