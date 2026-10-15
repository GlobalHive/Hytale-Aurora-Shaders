$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path $PSScriptRoot -Parent
$sdk = Join-Path $root 'third_party\Streamline'
$archive = Join-Path $sdk 'streamline-sdk-v2.14.1.zip'
$expectedHash = '92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b'
New-Item -ItemType Directory -Force $sdk | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest 'https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.14.1/streamline-sdk-v2.14.1.zip' -OutFile $archive -UseBasicParsing
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
    throw 'Streamline SDK checksum mismatch. No files were extracted.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $include = Join-Path $sdk 'include'
    $runtime = Join-Path $root 'shaderpack\streamline'
    New-Item -ItemType Directory -Force $include, $runtime | Out-Null
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName -match '^include/[^/]+\.h$') {
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $include ([IO.Path]::GetFileName($entry.FullName))), $true)
        }
    }
    foreach ($name in @('sl.interposer.dll', 'sl.common.dll', 'sl.reflex.dll', 'sl.pcl.dll', 'sl.dlss_g.dll', 'nvngx_dlssg.dll', 'nvngx_dlss.license.txt', 'reflex.license.txt')) {
        $entry = $zip.GetEntry("bin/x64/$name")
        if (-not $entry) { throw "Production SDK file missing: $name" }
        $destination = Join-Path $runtime $name
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $true)
        if ($name.EndsWith('.dll')) {
            $signature = Get-AuthenticodeSignature -LiteralPath $destination
            if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|,\s*)CN=NVIDIA Corporation(,|$)') {
                throw "Invalid NVIDIA production signature: $name"
            }
        }
    }
    foreach ($name in @('license.txt', '3rd-party-licenses.md')) {
        [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.GetEntry($name), (Join-Path $runtime "Streamline-$name"), $true)
    }
} finally { $zip.Dispose() }
Write-Host 'Streamline 2.14.1 headers and signed production Frame Generation runtimes prepared. Existing SR/RR runtimes were not changed.'
