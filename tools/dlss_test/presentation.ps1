param([switch]$Run)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'build\zig-cache\global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root 'build\zig-cache\local'
Push-Location $root
try {
    & tools\zig\zig.exe cc -target x86_64-windows-gnu -O2 -UNDEBUG -municode -DAU_HAS_DLSS=1 `
        -Ihook -Ithird_party\DLSS\include -Ithird_party\vulkan\include `
        tools\dlss_test\presentation_test.c hook\gl_stubs.c hook\aud_dlss.c -lgdi32 -luser32 -ladvapi32 -o build\dlss_presentation_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'DLSS presentation test compilation failed.' }
    if ($Run) {
        & build\dlss_presentation_test.exe (Join-Path $root 'shaderpack')
        if ($LASTEXITCODE -ne 0) { throw 'DLSS presentation regression detected.' }
    }
} finally { Pop-Location }
